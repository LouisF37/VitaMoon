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
#include "histogram.h"
#include "../config.h"
#include "../debug.h"
#include "../gui/guilib.h"
#include "../gui/theme.h"
#include "../power/vita.h"
#include "../thread_policy.h"

#include <Limelight.h>

#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <vita2d.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "../i18n.h"

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

/* ---- whole-session distributions (histogram.h), for the summary ---- */
static hist_t h_r2p = {.step_us = 500};        /* first packet -> visible: 0-256 ms */
static hist_t h_interval = {.step_us = 250};   /* between two shown frames: 0-128 ms */
static hist_t h_decode = {.step_us = 50};      /* hardware decode: 0-25.6 ms */
static uint64_t last_visible_us;               /* display thread */
static uint32_t interval_over;                 /* intervals over 1.5 frame times */

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
  acc_t r2p_us;        /* first packet of the shown frame -> visible on screen */
  acc_t aud_out_us;    /* sceAudioOutOutput blocking time */
  acc_t aud_buf_us;    /* audio buffered before output (low-latency mode) */
  uint32_t aud_underrun;
  uint32_t aud_skip_us;
  uint32_t replaced;     /* decoded frames never shown (display modes) */
  uint32_t idle;         /* SMOOTH: refreshes without a new frame */
  acc_t queue_depth;     /* frames waiting when the display picked one */
  uint32_t late;         /* SMOOTH: flips that missed their vertical blank */
  acc_t budget_us;       /* SMOOTH: time kept for drawing before the blank */
  uint32_t pull_try;     /* decoder catch-up attempts */
  uint32_t pull_got;     /* ... that returned a picture */
  acc_t aud_target_us;   /* audio cushion wanted (low-latency mode) */
  acc_t pace_delay_us;   /* PACED: delay added to the PC's capture times */
} c;

/* moonlight-common-c's video packet counters at the previous second (stats thread) */
static RTP_VIDEO_STATS rtp_prev;

/* Video-thread-only state */
static int last_frame_number = -1;
static uint64_t rx_ring_us[RX_RING];
static int rx_ring_fn[RX_RING];

/* Session state */
static volatile bool s_running = false;
static pthread_t s_thread;
static bool s_thread_created = false;
static SceUID s_fd = -1;
static int s_width, s_height, s_fps;

static char s_overlay[2][OVERLAY_LINES][OVERLAY_LINE_LEN];

/* Whole-session totals (stats thread only), saved per app at the end. */
static int s_app_id = 0;
static SceDateTime s_start_time;
static uint64_t sess_ms, sess_shown, sess_lost, sess_r2p_sum, sess_r2p_n;
static char s_info_path[640];   /* the session's .txt, completed at the end */
static volatile int s_overlay_idx = 0;
static volatile bool s_overlay_valid = false;
static int s_overlay_measured = -1;   /* display thread: which text s_overlay_width belongs to */
static int s_overlay_width = 0;

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
  hist_add(&h_decode, decode_us);
  if (result < 0) {
    cnt_add(&c.dec_err, 1);
    return;
  }
  if (!has_output)
    cnt_add(&c.dec_noout, 1);
  if (hold_frames != STREAM_STATS_HOLD_UNKNOWN)
    acc_add(&c.hold, hold_frames);
}

void stream_stats_on_pacer_drop(void) {
  cnt_add(&c.dropped, 1);
}

uint64_t stream_stats_receive_time(int frame_number) {
  int slot = frame_number & (RX_RING - 1);
  return rx_ring_fn[slot] == frame_number ? rx_ring_us[slot] : 0;
}

void stream_stats_on_present(uint32_t render_us, uint32_t swap_us, uint64_t receive_us, uint64_t visible_us) {
  cnt_add(&c.shown, 1);
  acc_add(&c.render_us, render_us);
  acc_add(&c.swap_us, swap_us);
  if (receive_us != 0 && visible_us >= receive_us) {
    acc_add(&c.r2p_us, (uint32_t)(visible_us - receive_us));
    hist_add(&h_r2p, (uint32_t)(visible_us - receive_us));
  }
  if (last_visible_us != 0 && visible_us > last_visible_us && visible_us - last_visible_us < HIST_GAP_US) {
    uint32_t interval = (uint32_t)(visible_us - last_visible_us);
    hist_add(&h_interval, interval);
    if (s_fps > 0 && interval * 2 > 3 * (1000000u / (uint32_t)s_fps))
      __atomic_fetch_add(&interval_over, 1, __ATOMIC_RELAXED);
  }
  last_visible_us = visible_us;
}

void stream_stats_on_decoder_pull(bool got_picture) {
  cnt_add(&c.pull_try, 1);
  if (got_picture)
    cnt_add(&c.pull_got, 1);
}

void stream_stats_on_display_late(void) {
  cnt_add(&c.late, 1);
}

void stream_stats_on_latch_budget(uint32_t budget_us) {
  acc_add(&c.budget_us, budget_us);
}

void stream_stats_on_pace(uint32_t delay_us) {
  acc_add(&c.pace_delay_us, delay_us);
}

void stream_stats_on_frame_replaced(void) {
  cnt_add(&c.replaced, 1);
}

void stream_stats_on_display_idle(void) {
  cnt_add(&c.idle, 1);
}

void stream_stats_on_display_queue(uint32_t ready) {
  acc_add(&c.queue_depth, ready);
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

void stream_stats_on_audio_fill(uint32_t buffered_us, uint32_t target_us) {
  acc_add(&c.aud_buf_us, buffered_us);
  acc_add(&c.aud_target_us, target_us);
}

void stream_stats_on_audio_underrun(void) {
  cnt_add(&c.aud_underrun, 1);
}

void stream_stats_on_audio_skip(uint32_t dropped_us) {
  cnt_add(&c.aud_skip_us, dropped_us);
}

/* ---- output ---- */

static void write_fd(SceUID fd, const char *s) {
  if (fd >= 0)
    sceIoWrite(fd, s, strlen(s));
}

/* Column legend written next to each session. */
static const char *k_legend =
  "CSV columns (one line per second; times in microseconds unless stated):\n"
  "  t_ms            time since the start of the session, in ms\n"
  "  win_ms          actual length of the measurement window (~1000 ms)\n"
  "  vblank          Vita screen refreshes during the window\n"
  "  recv            frames received from the PC\n"
  "  shown           frames shown\n"
  "  lost            frames that never arrived (gaps in frame numbers)\n"
  "  dropped         frames decoded but skipped by the original frame pacer\n"
  "  idr             full (key) frames received\n"
  "  dec_err         decoder errors\n"
  "  dec_noout       frames accepted by the decoder with no picture out\n"
  "  ooo             frame numbers going backwards (stream restart)\n"
  "  kbps            video bitrate received\n"
  "  rx_*            time to receive a whole frame (first -> last packet)\n"
  "  queue_*         wait between end of reception and decoding\n"
  "  host_*          capture + encode time on the PC (reported by Sunshine)\n"
  "  dec_*           duration of the decoder call\n"
  "  hold_avg_x100   pictures still inside the decoder after each frame, average x100 (150 = 1.5)\n"
  "  hold_max        pictures inside the decoder, maximum\n"
  "  render_*        drawing the frame (until the GPU is done)\n"
  "  swap_*          queuing the screen buffer flip\n"
  "  r2p_*           first packet received -> frame visible on screen, i.e. the\n"
  "                  vertical blank after the flip (Vita-side latency)\n"
  "  rtt_ms, rtt_var_ms  estimated ping and its variation\n"
  "  poor            \"poor connection\" warnings\n"
  "  pend_vid, pend_aud  video frames / audio packets waiting\n"
  "  aud_pkts, aud_err   audio packets decoded / lost or failed (concealed)\n"
  "  aud_out_*       time blocked in the audio output\n"
  "  cpu_mhz, gpu_mhz    CPU and GPU clock frequencies\n"
  "  aud_buf_*       audio buffered before output (low-latency audio)\n"
  "  aud_underrun    audio gaps: buffer ran empty (low-latency audio)\n"
  "  aud_skip_us     audio trimmed to bring the delay back (low-latency audio)\n"
  "  replaced        frames decoded but never shown (low latency / smooth display)\n"
  "  idle            screen refreshes with no new frame (smooth display)\n"
  "  disp_q_avg_x100, disp_q_max  frames waiting when the display picks one (average x100)\n"
  "  late            flips that missed their vertical blank (smooth display)\n"
  "  budget_avg_us   time kept for drawing before the vertical blank (smooth display)\n"
  "  pull_try, pull_got  decoder catch-up: extra reads tried / that returned a picture\n"
  "  aud_target_us   audio cushion wanted by the adaptive buffer (low-latency audio)\n"
  "  wifi_pct        Wi-Fi signal strength reported by the Vita, in percent (-1 = unknown)\n"
  "  pace_delay_us   margin of the paced display (see src/video/pacing.h)\n"
  "  fec_saved       video packets rebuilt by error correction\n"
  "  fec_failed      video frames given up: too many packets missing\n"
  "  pkt_ooo         video packets that arrived out of order\n"
  "  reorder_held    packets of the next frame held back, waiting for late ones\n"
  "  reorder_saved   frames completed by late packets thanks to that wait\n"
  "  *_avg = average over the second, *_max = maximum over the second\n"
  "The end of this file gives percentiles over the whole session: half of the\n"
  "values are below p50, 95 % below p95, 99 % below p99. Intervals between\n"
  "shown frames over 1.5 frame times are frames shown late or repeated.\n";

/* Creates <data>/stats/session-<date>.csv (integers only, ';' separated so that
 * Excel in a French locale opens it with a double click) and session-<date>.txt
 * (settings of the session + column legend). */
static void open_session_files(void) {
  char dir[512];
  char path[640];
  SceDateTime t;

  snprintf(dir, sizeof(dir), "%.400s/stats", config.key_dir);
  sceIoMkdir(dir, 0777); /* ignore "already exists" */

  sceRtcGetCurrentClockLocalTime(&t);
  char stamp[32];
  snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
           t.year, t.month, t.day, t.hour, t.minute, t.second);

  /* Session description */
  snprintf(path, sizeof(path), "%s/session-%s.txt", dir, stamp);
  SceUID info = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  snprintf(s_info_path, sizeof(s_info_path), "%s", info >= 0 ? path : "");
  if (info >= 0) {
    float refresh = 0.0f;
    sceDisplayGetRefreshRate(&refresh);
    char line[512];
    snprintf(line, sizeof(line),
             "vita-moonlight stream statistics\n"
             "build: %s\n"
             "date: %04d-%02d-%02d %02d:%02d:%02d\n"
             "stream: %dx%d @ %d fps, requested bitrate %d kbps, packets %d bytes\n",
             BUILD_ID, t.year, t.month, t.day, t.hour, t.minute, t.second,
             s_width, s_height, s_fps, config.stream.bitrate, config.stream.packetSize);
    write_fd(info, line);
    snprintf(line, sizeof(line),
             "settings: vblank_wait=%d frame_pacer=%d ref_frame_invalidation=%d center_region_only=%d\n"
             "display and clocks: boost_clocks=%d match_refresh_rate=%d (sent to host: %d) low_latency_audio=%d display_mode=%d (0 direct, 1 low latency, 2 smooth, 3 paced)\n"
             "Vita screen: %.4f Hz (rate reported by the system)\n"
             "clocks: cpu %d MHz, gpu %d MHz, bus %d MHz, xbar %d MHz\n\n",
             config.enable_vita_vblank_wait, config.enable_frame_pacer,
             config.enable_ref_frame_invalidation, config.center_region_only,
             config.boost_clocks, config.match_refresh_rate, config.stream.clientRefreshRateX100,
             config.low_latency_audio, config.display_mode,
             refresh, scePowerGetArmClockFrequency(), scePowerGetGpuClockFrequency(),
             scePowerGetBusClockFrequency(), scePowerGetGpuXbarClockFrequency());
    write_fd(info, line);
    SceNetCtlInfo net;
    int signal = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &net) >= 0 ? (int)net.rssi_percentage : -1;
    int channel = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_CHANNEL, &net) >= 0 ? (int)net.channel : -1;
    snprintf(line, sizeof(line),
             "this build: decoder_catchup=%d auto_bitrate=%d keep_link_open=%d smooth_edges=%d (4x multisampling in use: %d)\n"
             "performance: yuv_decode=%d reorder_wait_ms=%d thread_pinning=%d\n"
             "controls: read in step with the controller=%d\n"
             "Wi-Fi: signal %d %%, channel %d, power-save registry value %d (-1 = not readable; 1 is assumed to mean on)\n"
             "Bluetooth: %d (1 on, 0 off, -1 not readable)\n\n",
             config.decoder_catchup, config.auto_bitrate, config.http_keepalive, config.ui_msaa,
             gui_msaa_active(), config.decode_yuv, config.video_reorder_wait_ms, config.thread_pinning,
             config.input_sync, signal, channel, vitapower_wifi_power_save(), vitapower_bluetooth());
    write_fd(info, line);
    write_fd(info, k_legend);
    sceIoClose(info);
  }

  /* Per-second data */
  snprintf(path, sizeof(path), "%s/session-%s.csv", dir, stamp);
  s_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (s_fd < 0) {
    vita_debug_log("stream_stats: cannot open %s (0x%08x)\n", path, s_fd);
    return;
  }
  write_fd(s_fd, "t_ms;win_ms;vblank;recv;shown;lost;dropped;idr;dec_err;dec_noout;ooo;kbps;"
                 "rx_avg_us;rx_max_us;queue_avg_us;queue_max_us;host_avg_us;host_max_us;"
                 "dec_avg_us;dec_max_us;hold_avg_x100;hold_max;render_avg_us;render_max_us;"
                 "swap_avg_us;swap_max_us;r2p_avg_us;r2p_max_us;rtt_ms;rtt_var_ms;poor;"
                 "pend_vid;pend_aud;aud_pkts;aud_err;aud_out_avg_us;aud_out_max_us;cpu_mhz;gpu_mhz;"
                 "aud_buf_avg_us;aud_buf_max_us;aud_underrun;aud_skip_us;replaced;idle;disp_q_avg_x100;disp_q_max;"
                 "late;budget_avg_us;pull_try;pull_got;aud_target_us;wifi_pct;"
                 "pace_delay_us;fec_saved;fec_failed;pkt_ooo;reorder_held;reorder_saved\n");
  memset(&rtp_prev, 0, sizeof(rtp_prev));
}

/* Takes (and resets) every counter for the window that just ended, writes the
 * CSV line and prepares the 4 overlay lines. Runs on the stats thread only. */
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
  acc_snap_t aud_buf = acc_take(&c.aud_buf_us);
  uint32_t aud_underrun = cnt_take(&c.aud_underrun);
  uint32_t aud_skip_us = cnt_take(&c.aud_skip_us);
  uint32_t replaced = cnt_take(&c.replaced);
  uint32_t idle = cnt_take(&c.idle);
  acc_snap_t qd = acc_take(&c.queue_depth);
  uint32_t late = cnt_take(&c.late);
  acc_snap_t budget = acc_take(&c.budget_us);
  uint32_t pull_try = cnt_take(&c.pull_try);
  uint32_t pull_got = cnt_take(&c.pull_got);
  acc_snap_t aud_target = acc_take(&c.aud_target_us);

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
  acc_snap_t pace = acc_take(&c.pace_delay_us);
  /* Plain counters written by the video receive thread: a second's delta is
   * enough even if a read lands between two increments. */
  RTP_VIDEO_STATS rtp = *LiGetRTPVideoStats();
  uint32_t fec_saved = rtp.packetCountFecRecovered - rtp_prev.packetCountFecRecovered;
  uint32_t fec_failed = rtp.packetCountFecFailed - rtp_prev.packetCountFecFailed;
  uint32_t pkt_ooo = rtp.packetCountOOS - rtp_prev.packetCountOOS;
  uint32_t reorder_held = rtp.packetCountHeldForReorder - rtp_prev.packetCountHeldForReorder;
  uint32_t reorder_saved = rtp.frameCountReorderRescued - rtp_prev.frameCountReorderRescued;
  rtp_prev = rtp;
  SceNetCtlInfo net;
  int wifi_pct = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &net) >= 0 ? (int)net.rssi_percentage : -1;

  /* One CSV line: integers only, so no decimal-separator issue in Excel. */
  char line[768];
  snprintf(line, sizeof(line),
           "%u;%u;%d;%u;%u;%u;%u;%u;%u;%u;%u;%u;"
           "%u;%u;%u;%u;%u;%u;"
           "%u;%u;%u;%u;%u;%u;"
           "%u;%u;%u;%u;%u;%u;%u;"
           "%d;%d;%u;%u;%u;%u;%d;%d;"
           "%u;%u;%u;%u;%u;%u;%u;%u;"
           "%u;%u;%u;%u;%u;%d;"
           "%u;%u;%u;%u;%u;%u\n",
           (unsigned)(t_us / 1000), win_ms, vblanks,
           recv, shown, lost, dropped, idr, dec_err, dec_noout, ooo, kbps,
           rx.avg, rx.max, queue.avg, queue.max, host.avg, host.max,
           dec.avg, dec.max, hold.avg_x100, hold.max, render.avg, render.max,
           swap.avg, swap.max, r2p.avg, r2p.max, rtt, rtt_var, poor,
           pend_vid, pend_aud, aud_pkts, aud_err, aud_out.avg, aud_out.max, cpu, gpu,
           aud_buf.avg, aud_buf.max, aud_underrun, aud_skip_us, replaced, idle, qd.avg_x100, qd.max,
           late, budget.avg, pull_try, pull_got, aud_target.avg, wifi_pct,
           pace.avg, fec_saved, fec_failed, pkt_ooo, reorder_held, reorder_saved);
  write_fd(s_fd, line);

  sess_ms += win_ms;
  sess_shown += shown;
  sess_lost += lost;
  sess_r2p_sum += (uint64_t)r2p.avg * r2p.n;
  sess_r2p_n += r2p.n;

  /* Overlay: compact, refreshed once per second. */
  int next = s_overlay_idx ^ 1;
  float hz = win_us ? (float)vblanks * 1000000.0f / (float)win_us : 0.0f;
  snprintf(s_overlay[next][0], OVERLAY_LINE_LEN,
           "shown %u  recv %u  lost %u  skipped %u  late %u  idle %u   screen %.2f Hz",
           shown, recv, lost, dropped + replaced, late, idle, hz);
  snprintf(s_overlay[next][1], OVERLAY_LINE_LEN,
           "to screen %.1f ms (max %.1f)   decode %.1f   draw %.1f   in decoder %.2f",
           r2p.avg / 1000.0f, r2p.max / 1000.0f, dec.avg / 1000.0f,
           (render.avg + swap.avg) / 1000.0f, hold.avg_x100 / 100.0f);
  snprintf(s_overlay[next][2], OVERLAY_LINE_LEN,
           "network %.1f Mbps   ping %u ms (+-%u)   PC %.1f ms   key frames %u   late pkts %u (saved %u)",
           kbps / 1000.0f, rtt, rtt_var, host.avg / 1000.0f, idr, pkt_ooo, reorder_saved);
  snprintf(s_overlay[next][3], OVERLAY_LINE_LEN,
           "audio %.1f ms (target %.1f)  gaps %u   draw budget %.1f ms   errors %u   cpu %d MHz",
           aud_buf.avg / 1000.0f, aud_target.avg / 1000.0f, aud_underrun,
           budget.avg / 1000.0f, dec_err, cpu);
  s_overlay_idx = next;
  s_overlay_valid = true;
}

/* Wakes up every second (in 100 ms steps so the session can stop quickly) and
 * counts the screen refreshes (vblanks) elapsed since the previous window.
 * A pthread, since it calls into moonlight-common-c, whose locks are pthread
 * ones (dubleyu/vita-moonlight-relay saw crashes when kernel threads did). */
static void *stats_thread_main(void *argp) {
  thread_place(0, CORE_UI);
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
  return NULL;
}

/* ---- session ---- */

/* Called when the video decoder is set up (start of a stream) and torn down. */
void stream_stats_session_start(int width, int height, int fps) {
  if (s_running)
    stream_stats_session_end();

  memset(&c, 0, sizeof(c));
  sess_ms = sess_shown = sess_lost = sess_r2p_sum = sess_r2p_n = 0;
  hist_reset(&h_r2p);
  hist_reset(&h_interval);
  hist_reset(&h_decode);
  last_visible_us = 0;
  interval_over = 0;
  sceRtcGetCurrentClockLocalTime(&s_start_time);
  memset(rx_ring_us, 0, sizeof(rx_ring_us));
  for (int i = 0; i < RX_RING; i++)
    rx_ring_fn[i] = -1;
  last_frame_number = -1;
  s_overlay_valid = false;
  s_overlay_idx = 0;
  s_overlay_measured = -1;

  s_width = width;
  s_height = height;
  s_fps = fps;
  open_session_files();

  s_running = true;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 0x10000);
  int err = pthread_create(&s_thread, &attr, stats_thread_main, NULL);
  pthread_attr_destroy(&attr);
  if (err != 0) {
    vita_debug_log("stream_stats: cannot create thread (%d)\n", err);
    s_running = false;
    return;
  }
  s_thread_created = true;
}

void stream_stats_session_end(void) {
  bool was_running = s_running;
  s_running = false;
  if (s_thread_created) {
    pthread_join(s_thread, NULL);   /* the thread checks s_running every 100 ms */
    s_thread_created = false;
  }
  if (s_fd >= 0) {
    sceIoClose(s_fd);
    s_fd = -1;
  }
  s_overlay_valid = false;

  /* Percentiles of the whole session, at the end of its description file. */
  if (was_running && s_info_path[0]) {
    SceUID fd = sceIoOpen(s_info_path, SCE_O_WRONLY | SCE_O_APPEND, 0777);
    if (fd >= 0) {
      char line[256];
      snprintf(line, sizeof(line),
               "\nwhole session, %u s (percentiles: upper bound of buckets of 0.5 / 0.25 / 0.05 ms):\n",
               (unsigned)(sess_ms / 1000));
      write_fd(fd, line);
      hist_format(line, sizeof(line), "first packet -> visible (r2p)", &h_r2p);
      write_fd(fd, line);
      hist_format(line, sizeof(line), "interval between shown frames", &h_interval);
      write_fd(fd, line);
      uint32_t intervals = h_interval.n;
      snprintf(line, sizeof(line), "  %-32s %u of %u (%u.%u %%)\n", "intervals over 1.5 frame times",
               (unsigned)interval_over, (unsigned)intervals,
               (unsigned)(intervals ? (uint64_t)interval_over * 100 / intervals : 0),
               (unsigned)(intervals ? (uint64_t)interval_over * 1000 / intervals % 10 : 0));
      write_fd(fd, line);
      hist_format(line, sizeof(line), "hardware decode", &h_decode);
      write_fd(fd, line);
      sceIoClose(fd);
    }
  }

  /* Keep a short summary per app for the library screen (sessions >= 10 s). */
  if (was_running && s_app_id != 0 && sess_ms >= 10000) {
    char path[640], line[160];
    snprintf(path, sizeof(path), "%.400s/stats/last_%d.txt", config.key_dir, s_app_id);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) {
      snprintf(line, sizeof(line), "%u;%u;%u;%u;%04d-%02d-%02d %02d:%02d\n",
               (unsigned)(sess_shown * 10000 / sess_ms),
               (unsigned)(sess_r2p_n ? sess_r2p_sum / sess_r2p_n / 100 : 0),
               (unsigned)sess_lost, (unsigned)(sess_ms / 1000),
               s_start_time.year, s_start_time.month, s_start_time.day,
               s_start_time.hour, s_start_time.minute);
      sceIoWrite(fd, line, strlen(line));
      sceIoClose(fd);
    }
  }
}

void stream_stats_set_app(int app_id) {
  s_app_id = app_id;
}

bool stream_stats_read_last(int app_id, stream_last_session *out) {
  char path[640], buf[160];
  snprintf(path, sizeof(path), "%.400s/stats/last_%d.txt", config.key_dir, app_id);
  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (fd < 0)
    return false;
  int n = sceIoRead(fd, buf, sizeof(buf) - 1);
  sceIoClose(fd);
  if (n <= 0)
    return false;
  buf[n] = 0;
  unsigned fps, r2p, lost, secs;
  char when[20] = "";
  if (sscanf(buf, "%u;%u;%u;%u;%19[^\n]", &fps, &r2p, &lost, &secs, when) < 4)
    return false;
  out->shown_fps_x10 = (int)fps;
  out->r2p_ms_x10 = (int)r2p;
  out->lost = lost;
  out->seconds = secs;
  snprintf(out->when, sizeof(out->when), "%s", when);
  return true;
}

void stream_stats_draw_overlay(void) {
  if (!s_overlay_valid) {
    const char *wait = tr("stats: first second...");
    theme_rounded_rect(8, 8, theme_text_width(TS_SMALL, wait) + 22, 26, 8, TH_PANEL);
    theme_text(19, 26, TH_TEXT2, TS_SMALL, wait);
    return;
  }
  /* The text changes once per second: measure it once, not on every frame. */
  int idx = s_overlay_idx;
  if (idx != s_overlay_measured) {
    s_overlay_width = 0;
    for (int i = 0; i < OVERLAY_LINES; i++) {
      int lw = theme_text_width(TS_SMALL, s_overlay[idx][i]);
      if (lw > s_overlay_width) s_overlay_width = lw;
    }
    s_overlay_measured = idx;
  }
  theme_rounded_rect(8, 8, s_overlay_width + 22, OVERLAY_LINES * 19 + 12, 10, TH_PANEL);
  for (int i = 0; i < OVERLAY_LINES; i++)
    theme_text(19, 26 + i * 19, i == 0 ? TH_TEXT : TH_TEXT2, TS_SMALL, s_overlay[idx][i]);
}
