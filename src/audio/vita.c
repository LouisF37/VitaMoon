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

enum {
  VITA_AUDIO_INIT_OK        = 0,
  VITA_AUDIO_ERROR_BAD_OPUS = 0x80020001,
  VITA_AUDIO_ERROR_PORT     = 0x80020002,
};

/*
 * Two output modes (setting "Low-latency audio"):
 *
 * Classic (original): 5 ms packets are accumulated up to 20 ms, then handed to a
 * 20 ms audio port from the network receive thread, which blocks there until
 * the previous 20 ms have played.
 *
 * Low latency: decoded packets go into a ring buffer; a dedicated thread feeds a
 * 5.33 ms port (256 frames; the port size must be a multiple of 64 frames).
 * The receive thread never blocks on audio, playback starts with ~10 ms of
 * cushion, and accumulated delay (Wi-Fi bursts, clock drift between PC and
 * Vita) is dropped as soon as it exceeds 40 ms.
 */
#define FRAME_SIZE 240
#define VITA_SAMPLES 960
#define BUFFER_SIZE (2 * VITA_SAMPLES)

#define LL_GRAIN 256          /* frames per port write: 5.33 ms at 48 kHz */
#define LL_RING_FRAMES 4096   /* ring capacity: 85 ms (power of two) */
#define LL_PREBUFFER 512      /* cushion before (re)starting playback: 10.7 ms */
#define LL_MAX_FILL 1920      /* above 40 ms buffered, drop back to LL_PREBUFFER */
#define LL_MAX_PACKET 960     /* largest Opus packet accepted: 20 ms */

static int decode_offset;
static int port = -1;

static int active_audio_thread = true;
static OpusMSDecoder* decoder = NULL;

static short buffer[BUFFER_SIZE];

/* Low-latency mode state. Single producer (receive thread writes ll_head),
 * single consumer (output thread writes ll_tail): no lock needed. */
static bool ll_mode = false;
static short ll_ring[LL_RING_FRAMES * 2];
static uint32_t ll_head;
static uint32_t ll_tail;
static short ll_packet[LL_MAX_PACKET * 2];
static short ll_out[LL_GRAIN * 2];
static volatile bool ll_running = false;
static SceUID ll_thread = -1;

static inline uint32_t frames_to_us(uint32_t frames) {
  return frames * 125 / 6; /* 1e6 / 48000 */
}

static int ll_output_thread(SceSize args, void *argp) {
  bool playing = false;
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
      // (Re)start only once a small cushion is buffered.
      if (fill < LL_PREBUFFER) {
        sceKernelDelayThread(1000);
        continue;
      }
      playing = true;
    }
    if (fill > LL_MAX_FILL) {
      // Too much delay has built up: drop the oldest audio.
      uint32_t drop = fill - LL_PREBUFFER;
      tail += drop;
      fill -= drop;
      __atomic_store_n(&ll_tail, tail, __ATOMIC_RELEASE);
      stream_stats_on_audio_skip(frames_to_us(drop));
    }
    if (fill < LL_GRAIN) {
      // Underrun: the port plays silence until the cushion is rebuilt.
      stream_stats_on_audio_underrun();
      playing = false;
      continue;
    }

    stream_stats_on_audio_fill(frames_to_us(fill));
    for (int i = 0; i < LL_GRAIN; i++) {
      uint32_t idx = (tail + i) & (LL_RING_FRAMES - 1);
      ll_out[2 * i] = ll_ring[2 * idx];
      ll_out[2 * i + 1] = ll_ring[2 * idx + 1];
    }
    __atomic_store_n(&ll_tail, tail + LL_GRAIN, __ATOMIC_RELEASE);

    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    sceAudioOutOutput(port, ll_out); // blocks until the port accepts 256 more frames
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
  decoder = opus_multistream_decoder_create(opusConfig->sampleRate,
                                            opusConfig->channelCount,
                                            opusConfig->streams,
                                            opusConfig->coupledStreams,
                                            opusConfig->mapping,
                                            &rc);

  if (rc < 0) {
      return VITA_AUDIO_ERROR_BAD_OPUS;
  }

  // Both modes assume stereo (2 interleaved channels), as the original code did.
  ll_mode = config.low_latency_audio && opusConfig->channelCount == 2;
  decode_offset = 0;
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
    ll_thread = sceKernelCreateThread("vita_audio_out", ll_output_thread, 64, 0x4000, 0, 0, NULL);
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

static void ll_queue_packet(char* data, int length) {
  int n = opus_multistream_decode(decoder, data, length, ll_packet, LL_MAX_PACKET, 0);
  stream_stats_on_audio_packet(n > 0);
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

static void vita_renderer_decode_and_play_sample(char* data, int length) {
  if (!data)
    return;

  if (ll_mode) {
    ll_queue_packet(data, length);
    return;
  }

  // Classic mode: each packet carries 5 ms of sound (FRAME_SIZE samples). They are
  // accumulated up to VITA_SAMPLES (20 ms) before being handed to the audio port,
  // which blocks until the previous 20 ms have been played.
  int decodeLen = opus_multistream_decode(decoder, data, length, buffer + 2 * decode_offset, FRAME_SIZE, 0);
  stream_stats_on_audio_packet(decodeLen == FRAME_SIZE);
  if (decodeLen > 0) {
    if (decodeLen != FRAME_SIZE)
      return;
    decode_offset += decodeLen;

    if (decode_offset == VITA_SAMPLES) {
      decode_offset = 0;
      if (active_audio_thread) {
        SceUInt64 t0 = sceKernelGetProcessTimeWide();
        sceAudioOutOutput(port, buffer);
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
  .capabilities = CAPABILITY_DIRECT_SUBMIT,
};


void vitaaudio_start() {
  active_audio_thread = true;
}

void vitaaudio_stop() {
  active_audio_thread = false;
}
