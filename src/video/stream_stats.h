/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Per-second streaming statistics.
 *
 * Hot-path hooks only touch lock-free counters. A low-priority thread snapshots
 * them once per second, appends one CSV line to <key_dir>/stats/session-*.csv
 * and refreshes the on-screen overlay text, so measuring never blocks the
 * video, audio or input paths.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STREAM_STATS_HOLD_UNKNOWN 0xFFFFFFFFu

void stream_stats_session_start(int width, int height, int fps);
void stream_stats_session_end(void);

/* Video thread */
void stream_stats_on_frame(int frame_number, int frame_type, int length,
                           uint64_t receive_us, uint64_t enqueue_us,
                           uint16_t host_latency_tenth_ms, uint64_t submit_us);
void stream_stats_on_decode(uint32_t decode_us, int result, bool has_output, uint32_t hold_frames);
void stream_stats_on_pacer_drop(void);
void stream_stats_on_present(uint32_t render_us, uint32_t swap_us, uint64_t receive_us, uint64_t presented_us);
/* First-packet time of a frame recently seen by stream_stats_on_frame (0 if unknown). Receive thread only. */
uint64_t stream_stats_receive_time(int frame_number);
/* Display thread (LOW_LATENCY / SMOOTH modes) */
void stream_stats_on_frame_replaced(void);           /* decoded but never shown */
void stream_stats_on_display_idle(void);             /* SMOOTH: a refresh with no new frame */
void stream_stats_on_display_queue(uint32_t ready);  /* frames waiting when the display picked one */

/* Connection callbacks */
void stream_stats_on_conn_status(bool poor);

/* Audio thread */
void stream_stats_on_audio_packet(bool ok);
void stream_stats_on_audio_output(uint32_t block_us);
void stream_stats_on_audio_fill(uint32_t buffered_us);  /* low-latency mode */
void stream_stats_on_audio_underrun(void);
void stream_stats_on_audio_skip(uint32_t dropped_us);

/* Summary of the last session of an app, shown on the library screen. */
typedef struct {
  int shown_fps_x10;     /* average frames shown per second, x10 */
  int r2p_ms_x10;        /* average first packet -> screen, ms x10 */
  unsigned lost;         /* frames lost on the network during the session */
  unsigned seconds;      /* session length */
  char when[20];         /* "YYYY-MM-DD HH:MM" */
} stream_last_session;

/* App being streamed: its session summary is saved when the stream ends. */
void stream_stats_set_app(int app_id);
bool stream_stats_read_last(int app_id, stream_last_session *out);

/* Draws the latest per-second summary; call between vita2d_start/end_drawing. */
void stream_stats_draw_overlay(void);
