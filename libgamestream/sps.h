/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
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

#include <Limelight.h>

#define GS_SPS_BITSTREAM_FIXUP  0x01
#define GS_SPS_REMOVE_VST_FIXUP 0x02
#define GS_SPS_REMOVE_CLI_FIXUP 0x04

/* Sets up the SPS rewrite for a stream of that size; gs_sps_stop() releases it. */
void gs_sps_init(int width, int height);
void gs_sps_stop(void);

/* Largest rewritten SPS payload. The rewrite can be longer than the SPS
 * received, by less than this: the caller's buffer must keep that much room. */
#define GS_SPS_MAX_REWRITE 128

/* Appends the SPS, rewritten with low-latency hints, at out_buf + *out_offset
 * and advances *out_offset; appends it unchanged if it cannot be rewritten. */
void gs_sps_fix(PLENTRY sps, int flags, uint8_t* out_buf, uint32_t* out_offset);

/* num_ref_frames of the last SPS read before the rewrite set it to 1 (the
 * reference frames the PC's encoder declares); -1 before the first one. */
int gs_sps_source_ref_frames(void);
