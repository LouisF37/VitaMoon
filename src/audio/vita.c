/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2016 Ilya Zhuravlev
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "../audio.h"
#include "../config.h"
#include "../debug.h"

#include <stdio.h>
#include <string.h>
#include <opus/opus_multistream.h>
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include "../video/stream_stats.h"
#include "../thread_policy.h"

#include <Limelight.h>

enum {
  VITA_AUDIO_INIT_OK        = 0,
  VITA_AUDIO_ERROR_BAD_OPUS = 0x80020001,
  VITA_AUDIO_ERROR_PORT     = 0x80020002,
};

/*
 * Two output modes (setting "Low-latency audio"):
 *
 * Decoding runs on moonlight-common-c's AudioDec thread (the renderer declares
 * CAPABILITY_SLOW_OPUS_DECODER): the AudioRecv thread only reads the network,
 * as in dubleyu/vita-moonlight-relay and juanjsebgarcia/vita-moonlight.
 *
 * Classic (original): packets are accumulated up to 20 ms, then handed to a
 * 20 ms audio port, which blocks until the previous 20 ms have played. Two
 * blocks are used in turn, so a block is never rewritten while it may still be
 * playing. After a network stall, packets that queued up beyond 60 ms are
 * decoded (to keep the decoder's state) but not played, so the sound does not
 * stay behind the picture (dubleyu/vita-moonlight-relay).
 *
 * Low latency: decoded packets go into a ring buffer; a dedicated thread feeds a
 * 5.33 ms port (256 frames; the port size must be a multiple of 64 frames) and
 * the receive thread never blocks on audio. The ring is an adaptive jitter
 * buffer:
 *   - playback (re)starts once a small cushion ("target") is buffered;
 *   - each gap (the ring ran empty) raises the target by 5 ms, so the cushion
 *     grows to what the Wi-Fi link actually needs; it shrinks again by 1 ms
 *     per 30 s without a gap;
 *   - delay that builds up (a burst after a Wi-Fi stall, clock drift between
 *     PC and Vita) is trimmed back to the target with a short cross-fade,
 *     based on the smallest cushion seen over the last half second.
 *
 * In both modes a lost packet is replaced by Opus loss concealment, which
 * keeps the timing and avoids a hard gap.
 */
#define FRAME_SIZE 240
#define VITA_SAMPLES 960
#define BUFFER_SIZE (2 * VITA_SAMPLES)

#define LL_GRAIN 256            /* frames per port write: 5.33 ms at 48 kHz */
#define LL_RING_FRAMES 8192     /* ring capacity: 170 ms (power of two) */
#define LL_TARGET_START 480     /* cushion kept in the ring: 10 ms to begin with */
#define LL_TARGET_MIN 240       /* never below 5 ms */
#define LL_TARGET_MAX 2400      /* never above 50 ms */
#define LL_TARGET_STEP 240      /* +5 ms after each gap */
#define LL_TARGET_DECAY 48      /* -1 ms after LL_CALM_GRAINS without a gap */
#define LL_CALM_GRAINS 5625     /* 30 s of output */
#define LL_TRIM_SLACK 240       /* extra cushion tolerated before trimming: 5 ms */
#define LL_HARD_EXCESS 2880     /* trim at once above target + 60 ms */
#define LL_WINDOW_GRAINS 96     /* the cushion is judged over ~0.5 s */
#define LL_XFADE 64             /* cross-fade length when trimming: 1.3 ms */
#define LL_OUT_BUFFERS 4        /* output blocks used in rotation */
#define LL_MAX_PACKET 960       /* largest Opus packet accepted: 20 ms */

#define CLASSIC_CATCHUP_MS 60   /* audio queued beyond this is skipped (classic mode) */

static int decode_offset;
static int port = -1;
static int samples_per_frame = FRAME_SIZE;
static int classic_block = 0;     /* which of the two classic output blocks is filled */

static int active_audio_thread = true;
static OpusMSDecoder* decoder = NULL;

static short buffer[2][BUFFER_SIZE];

/* Low-latency mode state. Single producer (receive thread writes ll_head),
 * single consumer (output thread writes ll_tail): no lock needed. */
static bool ll_mode = false;
static short ll_ring[LL_RING_FRAMES * 2];
static uint32_t ll_head;
static uint32_t ll_tail;
static short ll_packet[LL_MAX_PACKET * 2];
/* The SDK does not say whether sceAudioOutOutput copies the block before it
 * returns, so a block is not rewritten until three more have been handed over
 * (same precaution as VitaRPS5). */
static short ll_out[LL_OUT_BUFFERS][LL_GRAIN * 2];
static volatile bool ll_running = false;
static SceUID ll_thread = -1;

static inline uint32_t frames_to_us(uint32_t frames) {
  return frames * 125 / 6; /* 1e6 / 48000 */
}

static inline void ring_read(uint32_t pos, short *left, short *right) {
  uint32_t idx = pos & (LL_RING_FRAMES - 1);
  *left = ll_ring[2 * idx];
  *right = ll_ring[2 * idx + 1];
}

static int ll_output_thread(SceSize args, void *argp) {
  thread_place(0, CORE_MEDIA);
  bool playing = false;
  uint32_t target = LL_TARGET_START;  /* cushion wanted in the ring, in frames */
  uint32_t win_min = 0xFFFFFFFF;      /* smallest cushion seen in the current window */
  int win_n = 0;
  uint32_t calm = 0;                  /* grains played since the last gap */
  int out_idx = 0;

  while (ll_running) {
    uint32_t tail = ll_tail;
    uint32_t fill = __atomic_load_n(&ll_head, __ATOMIC_ACQUIRE) - tail;

    if (!active_audio_thread) {
      // Stream paused: discard what arrives so playback resumes without delay.
      __atomic_store_n(&ll_tail, tail + fill, __ATOMIC_RELEASE);
      playing = false;
      sceKernelDelayThread(5000);
      continue;
    }
    if (!playing) {
      // (Re)start only once the cushion is there, and from the newest audio.
      if (fill < target + LL_GRAIN) {
        sceKernelDelayThread(1000);
        continue;
      }
      if (fill > target + LL_GRAIN) {
        uint32_t drop = fill - (target + LL_GRAIN);
        tail += drop;
        fill -= drop;
        __atomic_store_n(&ll_tail, tail, __ATOMIC_RELEASE);
        stream_stats_on_audio_skip(frames_to_us(drop));
      }
      playing = true;
      win_min = 0xFFFFFFFF;
      win_n = 0;
    }
    if (fill < LL_GRAIN) {
      // Gap: the ring ran empty. Ask for a larger cushion and rebuild it.
      stream_stats_on_audio_underrun();
      playing = false;
      calm = 0;
      if (target < LL_TARGET_MAX)
        target += LL_TARGET_STEP;
      continue;
    }

    // How much delay can be removed without touching the cushion we need?
    uint32_t cushion = fill - LL_GRAIN;
    uint32_t trim = 0;
    if (cushion > target + LL_HARD_EXCESS) {
      trim = cushion - target;
    } else {
      if (cushion < win_min)
        win_min = cushion;
      if (++win_n >= LL_WINDOW_GRAINS) {
        if (win_min > target + LL_TRIM_SLACK)
          trim = win_min - target;
        win_min = 0xFFFFFFFF;
        win_n = 0;
      }
    }

    short *out = ll_out[out_idx];
    out_idx = (out_idx + 1) % LL_OUT_BUFFERS;
    if (trim > 0) {
      // Jump ahead by `trim` frames, blending the two positions over LL_XFADE frames.
      for (int i = 0; i < LL_GRAIN; i++) {
        short nl, nr;
        ring_read(tail + trim + i, &nl, &nr);
        if (i < LL_XFADE) {
          short ol, or_;
          ring_read(tail + i, &ol, &or_);
          out[2 * i] = (short)((ol * (LL_XFADE - i) + nl * i) / LL_XFADE);
          out[2 * i + 1] = (short)((or_ * (LL_XFADE - i) + nr * i) / LL_XFADE);
        } else {
          out[2 * i] = nl;
          out[2 * i + 1] = nr;
        }
      }
      tail += trim;
      stream_stats_on_audio_skip(frames_to_us(trim));
      win_min = 0xFFFFFFFF;
      win_n = 0;
    } else {
      for (int i = 0; i < LL_GRAIN; i++)
        ring_read(tail + i, &out[2 * i], &out[2 * i + 1]);
    }
    __atomic_store_n(&ll_tail, tail + LL_GRAIN, __ATOMIC_RELEASE);
    stream_stats_on_audio_fill(frames_to_us(fill), frames_to_us(target));

    if (++calm >= LL_CALM_GRAINS) {
      calm = 0;
      if (target > LL_TARGET_MIN)
        target -= LL_TARGET_DECAY;
    }

    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    sceAudioOutOutput(port, out); // blocks until the port accepts 256 more frames
    stream_stats_on_audio_output((uint32_t)(sceKernelGetProcessTimeWide() - t0));
  }
  return 0;
}

static void vita_renderer_cleanup() {
  if (ll_running) {
    ll_running = false;
    SceUInt timeout = 500000;
    int thread_ret = 0;
    sceKernelWaitThreadEnd(ll_thread, &thread_ret, &timeout);
    sceKernelDeleteThread(ll_thread);
    ll_thread = -1;
  }
  // The original code never released the port, leaking one per stream.
  if (port >= 0) {
    sceAudioOutReleasePort(port);
    port = -1;
  }
  if (decoder != NULL) {
    opus_multistream_decoder_destroy(decoder);
    decoder = NULL;
  }
}

static int vita_renderer_init(int audioConfiguration, POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* audioContext, int arFlags) {
  int rc;
  // Both output paths write 2 interleaved channels: anything else would
  // overflow their buffers. The stream is always set up as stereo
  // (config.c), so this only guards against a future change there.
  if (opusConfig->channelCount != 2) {
    vita_debug_log("audio: %d channels, only stereo is supported\n", opusConfig->channelCount);
    return VITA_AUDIO_ERROR_BAD_OPUS;
  }
  decoder = opus_multistream_decoder_create(opusConfig->sampleRate,
                                            opusConfig->channelCount,
                                            opusConfig->streams,
                                            opusConfig->coupledStreams,
                                            opusConfig->mapping,
                                            &rc);

  if (rc < 0) {
      return VITA_AUDIO_ERROR_BAD_OPUS;
  }

  samples_per_frame = opusConfig->samplesPerFrame > 0 && opusConfig->samplesPerFrame <= LL_MAX_PACKET
                          ? opusConfig->samplesPerFrame : FRAME_SIZE;

  ll_mode = config.low_latency_audio;
  decode_offset = 0;
  classic_block = 0;
  // Classic mode fills 20 ms blocks with whole packets: the packet length must
  // divide 960 frames (5, 10 and 20 ms packets do).
  if (!ll_mode && (VITA_SAMPLES % samples_per_frame) != 0) {
    vita_debug_log("audio: %d-frame packets do not fit 20 ms blocks\n", samples_per_frame);
    opus_multistream_decoder_destroy(decoder);
    decoder = NULL;
    return VITA_AUDIO_ERROR_BAD_OPUS;
  }
  port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, ll_mode ? LL_GRAIN : VITA_SAMPLES,
                             48000, SCE_AUDIO_OUT_MODE_STEREO);

  if (port < 0) {
      vita_renderer_cleanup();
      return VITA_AUDIO_ERROR_PORT;
  }

  if (ll_mode) {
    ll_head = 0;
    ll_tail = 0;
    ll_running = true;
    ll_thread = sceKernelCreateThread("vita_audio_out", ll_output_thread, STREAM_THREAD_PRIORITY, 0x8000, 0, 0, NULL);
    if (ll_thread >= 0) {
      sceKernelStartThread(ll_thread, 0, NULL);
    } else {
      // Could not start the output thread: fall back to the classic mode.
      vita_debug_log("audio: output thread failed (0x%x), classic mode\n", ll_thread);
      ll_running = false;
      ll_mode = false;
      sceAudioOutSetConfig(port, VITA_SAMPLES, -1, (SceAudioOutMode)-1);
    }
  }

  vita_debug_log("open port 0x%x, low latency %d\n", port, ll_mode);
  return VITA_AUDIO_INIT_OK;
}

/* data == NULL: the packet was lost; Opus synthesises a replacement. */
static void ll_queue_packet(char* data, int length) {
  int n = data ? opus_multistream_decode(decoder, (const unsigned char *)data, length, ll_packet, LL_MAX_PACKET, 0)
               : opus_multistream_decode(decoder, NULL, 0, ll_packet, samples_per_frame, 0);
  stream_stats_on_audio_packet(data != NULL && n > 0);
  if (n <= 0) {
    vita_debug_log("Opus error from decode: %d\n", n);
    return;
  }
  uint32_t head = ll_head;
  uint32_t used = head - __atomic_load_n(&ll_tail, __ATOMIC_ACQUIRE);
  if (used + (uint32_t)n > LL_RING_FRAMES) {
    // Ring full (output stalled): drop this packet rather than block.
    stream_stats_on_audio_skip(frames_to_us((uint32_t)n));
    return;
  }
  for (int i = 0; i < n; i++) {
    uint32_t idx = (head + i) & (LL_RING_FRAMES - 1);
    ll_ring[2 * idx] = ll_packet[2 * i];
    ll_ring[2 * idx + 1] = ll_packet[2 * i + 1];
  }
  __atomic_store_n(&ll_head, head + (uint32_t)n, __ATOMIC_RELEASE);
}

/* Runs on moonlight-common-c's AudioDec thread (priority and core set in
 * thread_policy.c). data == NULL: a lost packet, concealed by Opus. */
static void vita_renderer_decode_and_play_sample(char* data, int length) {
  if (ll_mode) {
    ll_queue_packet(data, length);
    return;
  }

  // Classic mode: packets are accumulated up to VITA_SAMPLES (20 ms) before
  // being handed to the audio port, which blocks until the previous 20 ms have
  // been played.
  if (LiGetPendingAudioDuration() > CLASSIC_CATCHUP_MS) {
    // Behind after a stall: decode to keep the decoder's state, do not play.
    static short scratch[LL_MAX_PACKET * 2];
    int n = opus_multistream_decode(decoder, (const unsigned char *)data, data ? length : 0,
                                    scratch, samples_per_frame, 0);
    stream_stats_on_audio_packet(data != NULL && n > 0);
    if (n > 0)
      stream_stats_on_audio_skip(frames_to_us((uint32_t)n));
    return;
  }
  short *block = buffer[classic_block];
  int decodeLen = opus_multistream_decode(decoder, (const unsigned char *)data, data ? length : 0,
                                          block + 2 * decode_offset, samples_per_frame, 0);
  stream_stats_on_audio_packet(data != NULL && decodeLen == samples_per_frame);
  if (decodeLen > 0) {
    if (decodeLen != samples_per_frame)
      return;
    decode_offset += decodeLen;

    if (decode_offset == VITA_SAMPLES) {
      decode_offset = 0;
      classic_block ^= 1;   // the next 20 ms go into the other block
      if (active_audio_thread) {
        SceUInt64 t0 = sceKernelGetProcessTimeWide();
        sceAudioOutOutput(port, block);
        stream_stats_on_audio_output((uint32_t)(sceKernelGetProcessTimeWide() - t0));
      }
    }
  } else {
    vita_debug_log("Opus error from decode: %d\n", decodeLen);
  }
}

AUDIO_RENDERER_CALLBACKS audio_callbacks_vita = {
  .init = vita_renderer_init,
  .cleanup = vita_renderer_cleanup,
  .decodeAndPlaySample = vita_renderer_decode_and_play_sample,
  // Decode on common-c's AudioDec thread, not on the network receive thread.
  .capabilities = CAPABILITY_SLOW_OPUS_DECODER,
};


void vitaaudio_start() {
  active_audio_thread = true;
}

void vitaaudio_stop() {
  active_audio_thread = false;
}
