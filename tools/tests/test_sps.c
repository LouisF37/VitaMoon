/*
 * SPS rewrite (libgamestream/sps.c): the low-latency hints are set, the result
 * stays within the room the renderer keeps, and an SPS that cannot be parsed or
 * rewritten reaches the decoder unchanged instead of corrupted.
 */
#include "test.h"
#include "sps.h"
#include "h264_stream.h"

#include <stdlib.h>
#include <string.h>

/* A 960x544 High-profile SPS as an encoder would send it (built with the same
 * library, without the hints the rewrite adds). */
static int make_sps(uint8_t *out, int size) {
  h264_stream_t *h = h264_new();
  h->nal->nal_ref_idc = 3;
  h->nal->nal_unit_type = NAL_UNIT_TYPE_SPS;
  sps_t *s = h->sps;
  s->profile_idc = 100;
  s->level_idc = 50;
  s->seq_parameter_set_id = 0;
  s->chroma_format_idc = 1;
  s->log2_max_frame_num_minus4 = 4;
  s->pic_order_cnt_type = 2;
  s->num_ref_frames = 4;
  s->pic_width_in_mbs_minus1 = 59;          /* 960 */
  s->pic_height_in_map_units_minus1 = 33;   /* 544 */
  s->frame_mbs_only_flag = 1;
  s->direct_8x8_inference_flag = 1;
  s->vui_parameters_present_flag = 1;
  s->vui.video_signal_type_present_flag = 1;
  s->vui.video_format = 5;
  s->vui.colour_description_present_flag = 1;
  s->vui.colour_primaries = 6;
  s->vui.transfer_characteristics = 6;
  s->vui.matrix_coefficients = 6;
  out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
  int n = write_nal_unit(h, out + 4, size - 4);
  h264_free(h);
  return n > 0 ? n + 4 : -1;
}

int main(void) {
  TEST_BEGIN("sps rewrite");

  uint8_t sps_bytes[256];
  int sps_len = make_sps(sps_bytes, sizeof(sps_bytes));
  CHECK(sps_len > 8, "test SPS built");

  LENTRY entry = {0};
  entry.data = (char *)sps_bytes;
  entry.length = sps_len;
  entry.bufferType = BUFFER_TYPE_SPS;

  gs_sps_init(960, 544);
  CHECK(gs_sps_source_ref_frames() == -1, "no reference frame count before the first SPS");
  uint8_t out[512];
  memset(out, 0xEE, sizeof(out));
  uint32_t offset = 7;   /* appended after what is already in the buffer */
  gs_sps_fix(&entry, GS_SPS_BITSTREAM_FIXUP, out, &offset);
  uint32_t written = offset - 7;
  printf("    SPS %d bytes in, %u bytes out\n", sps_len, written);
  CHECK(out[6] == 0xEE, "nothing written before the offset");
  CHECK(written > 4 && written < (uint32_t)sps_len + GS_SPS_MAX_REWRITE, "result within the room kept by the renderer");
  CHECK(memcmp(out + 7, sps_bytes, 4) == 0, "start code kept");

  h264_stream_t *check = h264_new();
  int parsed = read_nal_unit(check, out + 7 + 4, (int)written - 4);
  CHECK(parsed > 0 && check->nal->nal_unit_type == NAL_UNIT_TYPE_SPS, "result is a valid SPS");
  CHECK(check->sps->num_ref_frames == 1, "one reference frame");
  CHECK(gs_sps_source_ref_frames() == 4, "the encoder's own reference frame count is kept for the log");
  CHECK(check->sps->vui.bitstream_restriction_flag == 1, "bitstream restriction present");
  CHECK(check->sps->vui.max_dec_frame_buffering == 1, "decoder buffers a single frame");
  CHECK(check->sps->vui.num_reorder_frames == 0, "no reordering");
  CHECK(check->sps->pic_width_in_mbs_minus1 == 59 && check->sps->pic_height_in_map_units_minus1 == 33,
        "picture size untouched");
  h264_free(check);

  /* Garbage after a valid start code: copied unchanged. */
  uint8_t junk[] = {0, 0, 0, 1, 0x67, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  entry.data = (char *)junk;
  entry.length = sizeof(junk);
  offset = 0;
  gs_sps_fix(&entry, GS_SPS_BITSTREAM_FIXUP, out, &offset);
  CHECK(offset == sizeof(junk) && memcmp(out, junk, sizeof(junk)) == 0, "unparsable SPS passed through unchanged");

  /* No start code, or too short: copied unchanged. */
  uint8_t bad[] = {1, 2, 3, 4, 5, 6};
  entry.data = (char *)bad;
  entry.length = sizeof(bad);
  offset = 0;
  gs_sps_fix(&entry, GS_SPS_BITSTREAM_FIXUP, out, &offset);
  CHECK(offset == sizeof(bad) && memcmp(out, bad, sizeof(bad)) == 0, "missing start code: passed through");
  uint8_t tiny[] = {0, 0, 1};
  entry.data = (char *)tiny;
  entry.length = sizeof(tiny);
  offset = 0;
  gs_sps_fix(&entry, GS_SPS_BITSTREAM_FIXUP, out, &offset);
  CHECK(offset == sizeof(tiny), "truncated SPS: passed through");

  /* A PPS where an SPS is expected: passed through, not rewritten as an SPS. */
  uint8_t pps[] = {0, 0, 1, 0x68, 0xEE, 0x3C, 0x80};
  entry.data = (char *)pps;
  entry.length = sizeof(pps);
  offset = 0;
  gs_sps_fix(&entry, GS_SPS_BITSTREAM_FIXUP, out, &offset);
  CHECK(offset == sizeof(pps) && memcmp(out, pps, sizeof(pps)) == 0, "not an SPS: passed through");

  gs_sps_stop();
  return TEST_END();
}
