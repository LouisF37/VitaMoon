#ifndef LATE_FRAMES_H
#define LATE_FRAMES_H

/*
 * "Skip late frames" (Settings > Tests (beta)): which decoded frames arrived
 * too late to be worth showing. Kept free of Vita calls so that it is tested
 * on a PC (tools/tests/test_late.c); src/video/vita.c calls it.
 *
 * A frame's lateness is measured when its first packet arrived: receive time
 * (Vita clock) minus capture time (the PC's clock, presentationTimeUs). The
 * two clocks have different origins and drift apart slowly (two crystals can
 * differ by about 100 ppm: 6 ms a minute), so the reference is the smallest
 * offset seen over the last one to two windows of LATE_WINDOW_US, the frame
 * that crossed the network fastest recently. The original Moonlight Vita had
 * a similar check from August to September 2026 (commit 0e9d823, removed by
 * f064cc5): smallest offset of the whole stream, and a frame over 100 ms late
 * was dropped undecoded with a request for a key frame.
 *
 * Only the network and the PC count: the time a picture then spends in the
 * decoder is not lateness. A still scene sent at 20 frames a second, which the
 * decoder holds longer, is therefore not "late".
 *
 * Late frames are still decoded (the next frames are built from them), only
 * not shown: the picture stays on the last frame shown, then jumps to the
 * present. If frames stay late for LATE_RESET_US, the delay has changed for
 * good: their offset becomes the new reference and they are shown again.
 */

#include <stdbool.h>
#include <stdint.h>

#define LATE_LIMIT_US      100000    /* later than this: not shown */
#define LATE_WINDOW_US     10000000  /* reference = smallest offset over 1-2 windows */
#define LATE_MIN_SAMPLES   30        /* frames seen before anything is hidden */
#define LATE_RESET_US      1000000   /* late for this long: new reference */

typedef enum {
  LATE_SHOW,        /* on time: show it */
  LATE_HIDE,        /* late: decode it but do not show it */
  LATE_NEW_REF,     /* late for too long: shown, and the reference starts again from it */
} late_verdict;

typedef struct {
  int64_t win_min[2];   /* smallest offset of the current and of the previous window */
  uint64_t win_start;   /* receive time at which the current window began */
  uint32_t samples;
  bool hiding;          /* the previous frame was hidden */
  uint64_t hide_first;  /* receive time of the first frame of the current run of hidden ones */
  /* The run of hidden frames in progress, for the log (late_run_*). */
  uint32_t run_hidden;
  uint32_t run_worst_us;
} late_state;

static inline void late_reset(late_state *s) {
  s->win_min[0] = s->win_min[1] = 0;
  s->win_start = 0;
  s->samples = 0;
  s->hiding = false;
  s->hide_first = 0;
  s->run_hidden = 0;
  s->run_worst_us = 0;
}

/* One decoded frame about to be shown. receive_us: first packet of the frame
 * (Vita clock, 0 if unknown); pts_us: its capture time on the PC. *late_us
 * receives its lateness (0 when unknown). */
static inline late_verdict late_check(late_state *s, uint64_t receive_us, uint64_t pts_us, uint32_t *late_us) {
  *late_us = 0;
  if (receive_us == 0)
    return LATE_SHOW;
  int64_t offset = (int64_t)receive_us - (int64_t)pts_us;
  if (s->samples == 0) {
    s->win_min[0] = s->win_min[1] = offset;
    s->win_start = receive_us;
  } else if (receive_us >= s->win_start && receive_us - s->win_start >= LATE_WINDOW_US) {
    s->win_min[1] = s->win_min[0];
    s->win_min[0] = offset;
    s->win_start = receive_us;
  } else if (offset < s->win_min[0]) {
    s->win_min[0] = offset;
  }
  if (s->samples < 0xFFFFFFFFu)
    s->samples++;

  int64_t ref = s->win_min[0] < s->win_min[1] ? s->win_min[0] : s->win_min[1];
  int64_t late = offset - ref;
  if (late < 0)
    late = 0;
  *late_us = late > 0xFFFFFFFFll ? 0xFFFFFFFFu : (uint32_t)late;

  if (s->samples <= LATE_MIN_SAMPLES || late <= LATE_LIMIT_US) {
    s->hiding = false;
    return LATE_SHOW;
  }
  if (!s->hiding) {
    s->hiding = true;
    s->hide_first = receive_us;
    s->run_hidden = 0;
    s->run_worst_us = 0;
  } else if (receive_us >= s->hide_first && receive_us - s->hide_first >= LATE_RESET_US) {
    /* Late for a whole second: this is the network's delay now. */
    s->win_min[0] = s->win_min[1] = offset;
    s->win_start = receive_us;
    s->hiding = false;
    return LATE_NEW_REF;
  }
  s->run_hidden++;
  if (*late_us > s->run_worst_us)
    s->run_worst_us = *late_us;
  return LATE_HIDE;
}

#endif // LATE_FRAMES_H
