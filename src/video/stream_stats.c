/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Per-second streaming statistics; see stream_stats.h.
 */
#include "stream_stats.h"
#include "../config.h"
#include "../debug.h"
#include "../gui/guilib.h"

#include <Limelight.h>

#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <vita2d.h>

#include <stdio.h>
#include <string.h>

#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

#define OVERLAY_LINES 4
#define OVERLAY_LINE_LEN 120
#define RX_RING 64

/* ---- lock-free accumulators (one writer thread, reset by the stats thread) ---- */

typedef struct {
  uint32_t sum;
  uint32_t max;
  uint32_t n;
} acc_t;

typedef struct {
  uint32_t avg;
  uint32_t avg_x100;
  uint32_t max;
  uint32_t n;
} acc_snap_t;

static inline void cnt_add(uint32_t *c, uint32_t v) {
  __atomic_fetch_add(c, v, __ATOMIC_RELAXED);
}

static inline uint32_t cnt_take(uint32_t *c) {
  return __atomic_exchange_n(c, 0, __ATOMIC_RELAXED);
}

static void acc_add(acc_t *a, uint32_t v) {
  __atomic_fetch_add(&a->sum, v, __ATOMIC_RELAXED);
  __atomic_fetch_add(&a->n, 1, __ATOMIC_RELAXED);
  uint32_t cur = __atomic_load_n(&a->max, __ATOMIC_RELAXED);
  while (v > cur &&
         !__atomic_compare_exchange_n(&a->max, &cur, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
  }
}

static acc_snap_t acc_take(acc_t *a) {
  acc_snap_t s;
  uint32_t sum = __atomic_exchange_n(&a->sum, 0, __ATOMIC_RELAXED);
  s.n = __atomic_exchange_n(&a->n, 0, __ATOMIC_RELAXED);
  s.max = __atomic_exchange_n(&a->max, 0, __ATOMIC_RELAXED);
  s.avg = s.n ? sum / s.n : 0;
  s.avg_x100 = s.n ? (uint32_t)(((uint64_t)sum * 100) / s.n) : 0;
  return s;
}

static struct {
  uint32_t recv;       /* decode units handed to the renderer */
  uint32_t lost;       /* gaps in frame numbers (never reached the renderer) */
  uint32_t ooo;        /* frame number went backwards (reset / reorder) */
  uint32_t idr;
  uint32_t bytes;
  uint32_t dec_err;    /* sceAvcdecDecode < 0 */
  uint32_t dec_noout;  /* decode accepted, no picture out (held by decoder) */
  uint32_t dropped;    /* decoded but skipped by the frame pacer */
  uint32_t shown;      /* presented (swap) */
  uint32_t poor;       /* "poor connection" notifications */
  uint32_t aud_pkts;
  uint32_t aud_err;
  acc_t rx_us;         /* enqueue - first packet: time to receive the whole frame */
  acc_t queue_us;      /* submit - enqueue */
  acc_t host_us;       /* host capture+encode latency reported by the host */
  acc_t dec_us;        /* sceAvcdecDecode call */
  acc_t hold;          /* decoder retention, in frames */
  acc_t render_us;     /* draw + wait_rendering_done */
  acc_t swap_us;       /* vita2d_swap_buffers */
  acc_t r2p_us;        /* first packet of the shown frame -> after swap */
  acc_t aud_out_us;    /* sceAudioOutOutput blocking time */
} c;

/* Video-thread-only state */
static int last_frame_number = -1;
static uint64_t rx_ring_us[RX_RING];
static int rx_ring_fn[RX_RING];

/* Session state */
static volatile bool s_running = false;
static SceUID s_thread = -1;
static SceUID s_fd = -1;
static int s_width, s_height, s_fps;

static char s_overlay[2][OVERLAY_LINES][OVERLAY_LINE_LEN];
static volatile int s_overlay_idx = 0;
static volatile bool s_overlay_valid = false;

/* ---- hooks ---- */

void stream_stats_on_frame(int frame_number, int frame_type, int length,
                           uint64_t receive_us, uint64_t enqueue_us,
                           uint16_t host_latency_tenth_ms, uint64_t submit_us) {
  cnt_add(&c.recv, 1);
  if (length > 0)
    cnt_add(&c.bytes, (uint32_t)length);
  if (frame_type == FRAME_TYPE_IDR)
    cnt_add(&c.idr, 1);

  if (last_frame_number >= 0) {
    if (frame_number > last_frame_number + 1)
      cnt_add(&c.lost, (uint32_t)(frame_number - last_frame_number - 1));
    else if (frame_number <= last_frame_number)
      cnt_add(&c.ooo, 1);
  }
  last_frame_number = frame_number;

  rx_ring_us[frame_number & (RX_RING - 1)] = receive_us;
  rx_ring_fn[frame_number & (RX_RING - 1)] = frame_number;

  if (enqueue_us >= receive_us && receive_us != 0)
    acc_add(&c.rx_us, (uint32_t)(enqueue_us - receive_us));
  if (submit_us >= enqueue_us && enqueue_us != 0)
    acc_add(&c.queue_us, (uint32_t)(submit_us - enqueue_us));
  if (host_latency_tenth_ms != 0)
    acc_add(&c.host_us, (uint32_t)host_latency_tenth_ms * 100);
}

void stream_stats_on_decode(uint32_t decode_us, int result, bool has_output, uint32_t hold_frames) {
  acc_add(&c.dec_us, decode_us);
  if (result < 0) {
    cnt_add(&c.dec_err, 1);
    return;
  }
  if (!has_output)
    cnt_add(&c.dec_noout, 1);
  else if (hold_frames != STREAM_STATS_HOLD_UNKNOWN)
    acc_add(&c.hold, hold_frames);
}

void stream_stats_on_pacer_drop(void) {
  cnt_add(&c.dropped, 1);
}

void stream_stats_on_present(uint32_t render_us, uint32_t swap_us, int shown_frame_number, uint64_t presented_us) {
  cnt_add(&c.shown, 1);
  acc_add(&c.render_us, render_us);
  acc_add(&c.swap_us, swap_us);
  int slot = shown_frame_number & (RX_RING - 1);
  if (rx_ring_fn[slot] == shown_frame_number && rx_ring_us[slot] != 0 && presented_us >= rx_ring_us[slot])
    acc_add(&c.r2p_us, (uint32_t)(presented_us - rx_ring_us[slot]));
}

void stream_stats_on_conn_status(bool poor) {
  if (poor)
    cnt_add(&c.poor, 1);
}

void stream_stats_on_audio_packet(bool ok) {
  cnt_add(ok ? &c.aud_pkts : &c.aud_err, 1);
}

void stream_stats_on_audio_output(uint32_t block_us) {
  acc_add(&c.aud_out_us, block_us);
}

/* ---- output ---- */

static void write_str(const char *s) {
  if (s_fd >= 0)
    sceIoWrite(s_fd, s, strlen(s));
}

static void open_session_file(void) {
  char dir[512];
  char path[640];
  SceDateTime t;

  snprintf(dir, sizeof(dir), "%s/stats", config.key_dir);
  sceIoMkdir(dir, 0777); /* ignore "already exists" */

  sceRtcGetCurrentClockLocalTime(&t);
  snprintf(path, sizeof(path), "%s/session-%04d%02d%02d-%02d%02d%02d.csv", dir,
           t.year, t.month, t.day, t.hour, t.minute, t.second);
  s_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (s_fd < 0) {
    vita_debug_log("stream_stats: cannot open %s (0x%08x)\n", path, s_fd);
    return;
  }

  float refresh = 0.0f;
  sceDisplayGetRefreshRate(&refresh);

  char line[512];
  snprintf(line, sizeof(line), "# vita-moonlight stream stats, build %s\n", BUILD_ID);
  write_str(line);
  snprintf(line, sizeof(line), "# date=%04d-%02d-%02d %02d:%02d:%02d\n",
           t.year, t.month, t.day, t.hour, t.minute, t.second);
  write_str(line);
  snprintf(line, sizeof(line), "# stream=%dx%d@%d bitrate_kbps=%d packet_size=%d\n",
           s_width, s_height, s_fps, config.stream.bitrate, config.stream.packetSize);
  write_str(line);
  snprintf(line, sizeof(line), "# vblank_wait=%d frame_pacer=%d ref_frame_invalidation=%d center_region_only=%d\n",
           config.enable_vita_vblank_wait, config.enable_frame_pacer,
           config.enable_ref_frame_invalidation, config.center_region_only);
  write_str(line);
  snprintf(line, sizeof(line), "# display_refresh_hz=%.4f cpu_mhz=%d gpu_mhz=%d bus_mhz=%d xbar_mhz=%d\n",
           refresh, scePowerGetArmClockFrequency(), scePowerGetGpuClockFrequency(),
           scePowerGetBusClockFrequency(), scePowerGetGpuXbarClockFrequency());
  write_str(line);
  write_str("# times in microseconds unless stated; hold in frames (avg x100); win_ms = actual window length\n");
  write_str("t_s,win_ms,vblank,recv,shown,lost,dropped,idr,dec_err,dec_noout,ooo,kbps,"
            "rx_avg_us,rx_max_us,queue_avg_us,queue_max_us,host_avg_us,host_max_us,"
            "dec_avg_us,dec_max_us,hold_avg_x100,hold_max,render_avg_us,render_max_us,"
            "swap_avg_us,swap_max_us,r2p_avg_us,r2p_max_us,rtt_ms,rtt_var_ms,poor,"
            "pend_vid,pend_aud,aud_pkts,aud_err,aud_out_avg_us,aud_out_max_us,cpu_mhz,gpu_mhz\n");
}

static void emit_second(uint64_t t_us, uint64_t win_us, int vblanks) {
  uint32_t recv = cnt_take(&c.recv);
  uint32_t lost = cnt_take(&c.lost);
  uint32_t ooo = cnt_take(&c.ooo);
  uint32_t idr = cnt_take(&c.idr);
  uint32_t bytes = cnt_take(&c.bytes);
  uint32_t dec_err = cnt_take(&c.dec_err);
  uint32_t dec_noout = cnt_take(&c.dec_noout);
  uint32_t dropped = cnt_take(&c.dropped);
  uint32_t shown = cnt_take(&c.shown);
  uint32_t poor = cnt_take(&c.poor);
  uint32_t aud_pkts = cnt_take(&c.aud_pkts);
  uint32_t aud_err = cnt_take(&c.aud_err);
  acc_snap_t rx = acc_take(&c.rx_us);
  acc_snap_t queue = acc_take(&c.queue_us);
  acc_snap_t host = acc_take(&c.host_us);
  acc_snap_t dec = acc_take(&c.dec_us);
  acc_snap_t hold = acc_take(&c.hold);
  acc_snap_t render = acc_take(&c.render_us);
  acc_snap_t swap = acc_take(&c.swap_us);
  acc_snap_t r2p = acc_take(&c.r2p_us);
  acc_snap_t aud_out = acc_take(&c.aud_out_us);

  uint32_t win_ms = (uint32_t)(win_us / 1000);
  uint32_t kbps = win_ms ? (uint32_t)(((uint64_t)bytes * 8) / win_ms) : 0;
  uint32_t rtt = 0, rtt_var = 0;
  if (!LiGetEstimatedRttInfo(&rtt, &rtt_var)) {
    rtt = 0;
    rtt_var = 0;
  }
  int pend_vid = LiGetPendingVideoFrames();
  int pend_aud = LiGetPendingAudioFrames();
  int cpu = scePowerGetArmClockFrequency();
  int gpu = scePowerGetGpuClockFrequency();

  char line[640];
  snprintf(line, sizeof(line),
           "%u.%03u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
           "%u,%u,%u,%u,%u,%u,"
           "%u,%u,%u,%u,%u,%u,"
           "%u,%u,%u,%u,%u,%u,%u,"
           "%d,%d,%u,%u,%u,%u,%d,%d\n",
           (unsigned)(t_us / 1000000), (unsigned)((t_us / 1000) % 1000), win_ms, vblanks,
           recv, shown, lost, dropped, idr, dec_err, dec_noout, ooo, kbps,
           rx.avg, rx.max, queue.avg, queue.max, host.avg, host.max,
           dec.avg, dec.max, hold.avg_x100, hold.max, render.avg, render.max,
           swap.avg, swap.max, r2p.avg, r2p.max, rtt, rtt_var, poor,
           pend_vid, pend_aud, aud_pkts, aud_err, aud_out.avg, aud_out.max, cpu, gpu);
  write_str(line);

  /* Overlay: compact, refreshed once per second. */
  int next = s_overlay_idx ^ 1;
  float hz = win_us ? (float)vblanks * 1000000.0f / (float)win_us : 0.0f;
  snprintf(s_overlay[next][0], OVERLAY_LINE_LEN,
           "screen %.2fHz  shown %u  recv %u  lost %u  drop %u  idr %u  err %u",
           hz, shown, recv, lost, dropped, idr, dec_err);
  snprintf(s_overlay[next][1], OVERLAY_LINE_LEN,
           "decode %.1f/%.1fms  hold %.2f/%uf  render %.1f/%.1fms  swap %.1f/%.1fms",
           dec.avg / 1000.0f, dec.max / 1000.0f, hold.avg_x100 / 100.0f, hold.max,
           render.avg / 1000.0f, render.max / 1000.0f, swap.avg / 1000.0f, swap.max / 1000.0f);
  snprintf(s_overlay[next][2], OVERLAY_LINE_LEN,
           "rx %.1f/%.1fms  host %.1f/%.1fms  rx>shown %.1f/%.1fms",
           rx.avg / 1000.0f, rx.max / 1000.0f, host.avg / 1000.0f, host.max / 1000.0f,
           r2p.avg / 1000.0f, r2p.max / 1000.0f);
  snprintf(s_overlay[next][3], OVERLAY_LINE_LEN,
           "rtt %u+-%ums  %.1fMbps  poor %u  audio %u pkt  cpu %dMHz",
           rtt, rtt_var, kbps / 1000.0f, poor, aud_pkts, cpu);
  s_overlay_idx = next;
  s_overlay_valid = true;
}

static int stats_thread_main(SceSize args, void *argp) {
  uint64_t t_start = sceKernelGetProcessTimeWide();
  uint64_t t_prev = t_start;
  int vc_prev = sceDisplayGetVcount();

  while (s_running) {
    uint64_t target = t_prev + 1000000;
    while (s_running) {
      uint64_t now = sceKernelGetProcessTimeWide();
      if (now >= target)
        break;
      uint64_t rem = target - now;
      sceKernelDelayThread(rem > 100000 ? 100000 : (SceUInt)rem);
    }
    if (!s_running)
      break;

    uint64_t now = sceKernelGetProcessTimeWide();
    int vc = sceDisplayGetVcount();
    emit_second(now - t_start, now - t_prev, vc - vc_prev);
    t_prev = now;
    vc_prev = vc;
  }
  return 0;
}

/* ---- session ---- */

void stream_stats_session_start(int width, int height, int fps) {
  if (s_running)
    stream_stats_session_end();

  memset(&c, 0, sizeof(c));
  memset(rx_ring_us, 0, sizeof(rx_ring_us));
  for (int i = 0; i < RX_RING; i++)
    rx_ring_fn[i] = -1;
  last_frame_number = -1;
  s_overlay_valid = false;

  s_width = width;
  s_height = height;
  s_fps = fps;
  open_session_file();

  s_running = true;
  s_thread = sceKernelCreateThread("stream_stats", stats_thread_main, 0x10000100, 0x10000, 0, 0, NULL);
  if (s_thread < 0) {
    vita_debug_log("stream_stats: cannot create thread (0x%08x)\n", s_thread);
    s_running = false;
    return;
  }
  sceKernelStartThread(s_thread, 0, NULL);
}

void stream_stats_session_end(void) {
  bool was_running = s_running;
  s_running = false;
  if (was_running && s_thread >= 0) {
    SceUInt timeout = 2000000;
    int thread_ret = 0;
    sceKernelWaitThreadEnd(s_thread, &thread_ret, &timeout);
    sceKernelDeleteThread(s_thread);
  }
  s_thread = -1;
  if (s_fd >= 0) {
    sceIoClose(s_fd);
    s_fd = -1;
  }
  s_overlay_valid = false;
}

void stream_stats_draw_overlay(void) {
  const unsigned int white = RGBA8(0xFF, 0xFF, 0xFF, 0xFF);
  if (!s_overlay_valid) {
    vita2d_draw_rectangle(6, 6, 300, 24, RGBA8(0, 0, 0, 0xA0));
    vita2d_font_draw_text(font, 12, 24, white, 16, "stats: first second...");
    return;
  }
  int idx = s_overlay_idx;
  vita2d_draw_rectangle(6, 6, 640, OVERLAY_LINES * 20 + 8, RGBA8(0, 0, 0, 0xA0));
  for (int i = 0; i < OVERLAY_LINES; i++)
    vita2d_font_draw_text(font, 12, 24 + i * 20, white, 16, s_overlay[idx][i]);
}
