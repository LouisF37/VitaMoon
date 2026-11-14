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
void stream_stats_on_present(uint32_t render_us, uint32_t swap_us, int shown_frame_number, uint64_t presented_us);

/* Connection callbacks */
void stream_stats_on_conn_status(bool poor);

/* Audio thread */
void stream_stats_on_audio_packet(bool ok);
void stream_stats_on_audio_output(uint32_t block_us);

/* Draws the latest per-second summary; call between vita2d_start/end_drawing. */
void stream_stats_draw_overlay(void);
