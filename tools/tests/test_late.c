/*
 * "Skip late frames" (src/video/late_frames.h) against simulated streams:
 * steady with Wi-Fi jitter, with the clocks drifting either way, a still scene
 * at 20 fps, a Wi-Fi freeze followed by a burst, and a delay that grows for
 * good.
 */
#include "test.h"
#include "late_frames.h"

#include <stdlib.h>

typedef struct {
  int fps;
  double ppm;            /* Vita clock speed relative to the PC's, parts per million */
  uint32_t transit_us;   /* fixed network time */
  uint32_t jitter_us;    /* extra random delay, 0..jitter_us */
  int seconds;
  int freeze_at_s;       /* nothing arrives for freeze_ms, then everything at once (0: none) */
  int freeze_ms;
  int shift_at_s;        /* from then on, the network takes shift_ms more (0: none) */
  int shift_ms;
} sim;

typedef struct {
  int frames, hidden, new_ref, hidden_in_freeze, hidden_outside;
  uint32_t worst_shown_late_us;   /* lateness of the latest frame still shown during the freeze */
} result;

static result run(const sim *p, unsigned seed) {
  srand(seed);
  late_state s;
  late_reset(&s);
  result r = {0};
  const uint64_t base = 5000000000ull;    /* the Vita clock's origin is unrelated to the PC's */
  int n = p->seconds * p->fps;
  for (int i = 0; i < n; i++) {
    uint64_t pts = (uint64_t)i * 1000000u / (uint64_t)p->fps;
    double vita_pts = (double)pts * (1.0 + p->ppm * 1e-6);
    uint64_t arrive = base + (uint64_t)vita_pts + p->transit_us +
                      (p->jitter_us ? (uint64_t)(rand() % (int)p->jitter_us) : 0);
    bool in_freeze = false;
    if (p->freeze_at_s) {
      uint64_t f0 = (uint64_t)p->freeze_at_s * 1000000u;
      uint64_t f1 = f0 + (uint64_t)p->freeze_ms * 1000u;
      if (pts >= f0 && pts < f1) {
        in_freeze = true;
        /* held back by the access point, then delivered one per millisecond */
        arrive = base + (uint64_t)((double)f1 * (1.0 + p->ppm * 1e-6)) + p->transit_us + (pts - f0) / 16;
      }
    }
    if (p->shift_at_s && pts >= (uint64_t)p->shift_at_s * 1000000u)
      arrive += (uint64_t)p->shift_ms * 1000u;
    uint32_t late_us;
    late_verdict v = late_check(&s, arrive, pts, &late_us);
    r.frames++;
    if (v == LATE_HIDE) {
      r.hidden++;
      if (in_freeze) r.hidden_in_freeze++; else r.hidden_outside++;
    } else if (v == LATE_NEW_REF) {
      r.new_ref++;
    } else if (in_freeze && late_us > r.worst_shown_late_us) {
      r.worst_shown_late_us = late_us;
    }
  }
  return r;
}

int main(void) {
  TEST_BEGIN("late frames");

  sim steady = {.fps = 60, .ppm = 0, .transit_us = 4000, .jitter_us = 30000, .seconds = 600};
  result r = run(&steady, 1);
  CHECK(r.hidden == 0 && r.new_ref == 0, "steady stream with 30 ms of jitter: nothing hidden");

  sim fast = steady;
  fast.ppm = 150;      /* 90 ms of drift over the 10 minutes */
  r = run(&fast, 2);
  CHECK(r.hidden == 0 && r.new_ref == 0, "Vita clock 150 ppm fast: drift never counts as lateness");
  sim slow = steady;
  slow.ppm = -150;
  r = run(&slow, 3);
  CHECK(r.hidden == 0 && r.new_ref == 0, "Vita clock 150 ppm slow: nothing hidden");

  sim still = steady;
  still.fps = 20;
  r = run(&still, 4);
  CHECK(r.hidden == 0, "still scene at 20 fps: nothing hidden");

  /* A 700 ms freeze: the frames captured during it arrive together at its end. */
  sim freeze = steady;
  freeze.seconds = 120;
  freeze.freeze_at_s = 60;
  freeze.freeze_ms = 700;
  r = run(&freeze, 5);
  printf("    freeze 700 ms: %d of %d frames of the freeze hidden, %d outside, latest shown %u ms late\n",
         r.hidden_in_freeze, 42, r.hidden_outside, r.worst_shown_late_us / 1000);
  /* Captured at 60.000-60.700 s and all delivered at ~60.700 s: those captured
   * more than ~100 ms (minus jitter) before the end are hidden. */
  CHECK(r.hidden_in_freeze >= 33 && r.hidden_in_freeze <= 40, "freeze: the frames over 100 ms late are hidden");
  CHECK(r.hidden_outside == 0, "freeze: no frame hidden outside it");
  CHECK(r.worst_shown_late_us <= LATE_LIMIT_US, "freeze: no frame shown over 100 ms late");
  CHECK(r.new_ref == 0, "freeze: the reference is kept");

  /* A 2.7 s freeze (the longest of the 1.0.0-beta.11 session): its 162 frames
   * arrive within 0.2 s, all but the last few hidden, and the reference is kept. */
  freeze.freeze_ms = 2700;
  r = run(&freeze, 6);
  printf("    freeze 2.7 s: %d hidden, %d new reference\n", r.hidden, r.new_ref);
  CHECK(r.hidden_in_freeze >= 150 && r.hidden_outside == 0 && r.new_ref == 0,
        "long freeze: the stale frames hidden, the reference kept");
  CHECK(r.worst_shown_late_us <= LATE_LIMIT_US, "long freeze: no frame shown over 100 ms late");

  /* The network takes 300 ms more for good (congested Wi-Fi): hidden for about
   * a second, then a new reference and the picture comes back. */
  sim shift = steady;
  shift.seconds = 120;
  shift.shift_at_s = 60;
  shift.shift_ms = 300;
  r = run(&shift, 7);
  printf("    delay +300 ms for good: %d hidden, %d new reference\n", r.hidden, r.new_ref);
  CHECK(r.new_ref == 1, "delay grows for good: one new reference");
  CHECK(r.hidden >= 55 && r.hidden <= 62, "delay grows for good: about one second hidden");

  /* Nothing hidden before LATE_MIN_SAMPLES frames, nor with no receive time. */
  late_state s;
  late_reset(&s);
  uint32_t late_us;
  int hidden = 0;
  for (int i = 0; i < LATE_MIN_SAMPLES; i++) {
    uint64_t pts = (uint64_t)i * 16667u;
    if (late_check(&s, 1000000 + pts + (i == 5 ? 0 : (i % 2 ? 500000 : 0)), pts, &late_us) != LATE_SHOW)
      hidden++;
  }
  CHECK(hidden == 0, "nothing hidden during the first frames");
  CHECK(late_check(&s, 0, 123, &late_us) == LATE_SHOW && late_us == 0, "unknown receive time: shown");

  return TEST_END();
}
