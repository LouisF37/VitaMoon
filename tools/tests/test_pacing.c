/*
 * Paced display (src/video/pacing.c) against simulated streams: steady, with
 * Wi-Fi jitter, with the PC's clock drifting either way, at 30 fps, across a
 * pause, and when the display falls behind.
 *
 * The simulation follows the video code: a frame is observed when it has been
 * decoded, and once per Vita screen refresh (59.94 Hz) the display picks the
 * frame for the next flip a few milliseconds before the vertical blank.
 */
#include "test.h"
#include "pacing.h"

#include <stdlib.h>

#define MAX_FRAMES 8000
#define VITA_PERIOD_US 16683   /* 59.94 Hz */
#define LATCH_BEFORE_US 4000   /* the display picks this long before the blank */

typedef struct {
  uint64_t pts, arrive, shown_at;
  bool observed, shown, dropped;
} sim_frame;

typedef struct {
  double pc_fps;
  double pc_ppm;        /* PC clock speed error, parts per million */
  uint32_t transit_us;  /* fixed network + decode time */
  uint32_t jitter_us;   /* extra random delay, 0..jitter_us */
  int seconds;
  int pause_at_s;       /* PC stops sending for pause_len_s (0: no pause) */
  int pause_len_s;
} sim_params;

typedef struct {
  int frames, shown, dropped, never;
  int idle_late;                            /* refreshes without a new frame, last half */
  uint32_t lat_max_early, lat_max_late;     /* first / last fifth of the stream */
  uint64_t lat_sum_late;
  int lat_n_late;
  uint32_t final_delay;
  int out_of_order;
  int frames_after_pause_wait;              /* refreshes until the first frame after a pause */
} sim_result;

static uint32_t rng = 12345;
static uint32_t rnd(uint32_t max) {
  rng = rng * 1103515245u + 12345u;
  return max ? (rng >> 8) % (max + 1) : 0;
}

static sim_frame F[MAX_FRAMES];

static sim_result simulate(const sim_params *sp) {
  sim_result r = {0};
  pace_state p;
  pace_reset(&p);
  rng = 12345;

  int n = (int)(sp->pc_fps * sp->seconds);
  if (n > MAX_FRAMES) n = MAX_FRAMES;
  double interval_us = 1e6 / sp->pc_fps;
  uint64_t pts0 = 5000000000ull;   /* PC clock origin, unrelated to the Vita's */
  uint64_t local0 = 1000000;
  uint64_t prev_arrive = 0;
  int m = 0;
  for (int i = 0; i < n; i++) {
    double t = i * interval_us;
    if (sp->pause_len_s && t >= sp->pause_at_s * 1e6 && t < (sp->pause_at_s + sp->pause_len_s) * 1e6)
      continue;   /* nothing captured while the PC is paused */
    F[m].pts = pts0 + (uint64_t)t;
    uint64_t captured_local = local0 + (uint64_t)(t * (1.0 + sp->pc_ppm * 1e-6));
    uint64_t a = captured_local + sp->transit_us + rnd(sp->jitter_us);
    if (a < prev_arrive) a = prev_arrive;   /* frames are decoded in order */
    prev_arrive = a;
    F[m].arrive = a;
    F[m].observed = F[m].shown = F[m].dropped = false;
    m++;
  }
  r.frames = m;

  int next_obs = 0;
  uint64_t last_shown_pts = 0;
  int window_lo = 0;             /* first frame not yet shown or dropped */
  uint64_t end = F[m - 1].arrive + 200000;
  int pause_first = -1;          /* index of the first frame after the pause */
  uint64_t pause_resume_arrive = 0;
  if (sp->pause_len_s) {
    for (int i = 1; i < m; i++)
      if (F[i].pts - F[i - 1].pts > 1000000) { pause_first = i; pause_resume_arrive = F[i].arrive; break; }
  }
  for (uint64_t vblank = local0; vblank < end; vblank += VITA_PERIOD_US) {
    uint64_t deadline = vblank + VITA_PERIOD_US;
    uint64_t latch = deadline - LATCH_BEFORE_US;
    while (next_obs < m && F[next_obs].arrive <= latch) {
      pace_observe(&p, F[next_obs].pts, F[next_obs].arrive, LATCH_BEFORE_US);
      F[next_obs].observed = true;
      next_obs++;
    }
    pace_frame frames[32];
    int idx[32];
    int k = 0;
    for (int i = window_lo; i < next_obs && k < 32; i++) {
      if (F[i].shown || F[i].dropped) continue;
      frames[k].ready = true;
      frames[k].pts_us = F[i].pts;
      frames[k].seq = (uint32_t)i;
      idx[k++] = i;
    }
    uint32_t drop = 0;
    int pick = pace_pick(&p, frames, k, deadline, false, &drop);
    for (int j = 0; j < k; j++)
      if (drop & (1u << j)) { F[idx[j]].dropped = true; r.dropped++; }
    if (pick < 0 && next_obs > m / 2 && next_obs < m &&
        (pause_first < 0 || F[next_obs].arrive < F[pause_first].arrive - 1000000 ||
         F[next_obs].arrive > F[pause_first].arrive + 1000000))
      r.idle_late++;
    if (pick >= 0) {
      sim_frame *f = &F[idx[pick]];
      f->shown = true;
      f->shown_at = deadline;
      r.shown++;
      if (f->pts < last_shown_pts) r.out_of_order++;
      last_shown_pts = f->pts;
      if (pause_first >= 0 && idx[pick] == pause_first)
        r.frames_after_pause_wait = (int)((deadline - pause_resume_arrive) / VITA_PERIOD_US);
    }
    while (window_lo < next_obs && (F[window_lo].shown || F[window_lo].dropped)) window_lo++;
  }

  for (int i = 0; i < m; i++) {
    if (!F[i].shown && !F[i].dropped) r.never++;
    if (!F[i].shown) continue;
    uint32_t lat = (uint32_t)(F[i].shown_at - F[i].arrive);
    if (i < m / 5 && lat > r.lat_max_early) r.lat_max_early = lat;
    if (i >= m * 4 / 5) {
      if (lat > r.lat_max_late) r.lat_max_late = lat;
      r.lat_sum_late += lat;
      r.lat_n_late++;
    }
  }
  r.final_delay = p.delay_us;
  return r;
}

static void print(const char *name, const sim_result *r) {
  printf("    %-24s frames %5d shown %5d dropped %3d repeats %4d | wait max early %5.1f, late %5.1f, "
         "avg late %5.1f ms | margin %4.1f ms\n",
         name, r->frames, r->shown, r->dropped, r->idle_late, r->lat_max_early / 1000.0, r->lat_max_late / 1000.0,
         r->lat_n_late ? r->lat_sum_late / 1000.0 / r->lat_n_late : 0.0, r->final_delay / 1000.0);
}

int main(void) {
  TEST_BEGIN("pacing");

  /* 60 fps stream on a 59.94 Hz screen: the PC sends 0.1 % more frames than
   * the screen can show, so about one frame in a thousand must go. */
  sim_params steady = {.pc_fps = 60, .transit_us = 6000, .seconds = 60};
  sim_result r = simulate(&steady);
  print("steady 60 fps", &r);
  CHECK(r.out_of_order == 0, "frames shown in order");
  CHECK(r.never <= 2, "every frame shown or dropped");
  CHECK(r.dropped <= r.frames / 500, "at most 0.2 % dropped on a steady link");
  CHECK(r.final_delay == PACE_DELAY_MIN_US, "margin comes down to the minimum when nothing is late");
  CHECK(r.idle_late <= r.frames / 200, "steady: almost no repeated frame");
  CHECK(r.lat_max_late <= PACE_DELAY_MIN_US + 2 * VITA_PERIOD_US, "wait stays within the delay plus two refreshes");

  /* Wi-Fi jitter of up to 15 ms: the delay grows to absorb it. */
  sim_params jitter = {.pc_fps = 60, .transit_us = 6000, .jitter_us = 15000, .seconds = 60};
  r = simulate(&jitter);
  print("60 fps, 0-15 ms jitter", &r);
  CHECK(r.out_of_order == 0, "frames shown in order");
  CHECK(r.idle_late <= r.frames / 200, "even cadence despite jitter: almost no repeated frame");
  CHECK(r.final_delay >= PACE_DELAY_MIN_US && r.final_delay <= PACE_DELAY_MAX_US, "margin within its bounds");
  CHECK(r.dropped <= r.frames / 20, "at most 5 % dropped with heavy jitter");
  CHECK(r.lat_max_late <= PACE_DELAY_MAX_US + 2 * VITA_PERIOD_US, "wait bounded by the maximum delay");

  /* The PC's clock runs 200 ppm fast, then 200 ppm slow (crystal clocks are
   * within about 100 ppm): the anchor must follow instead of letting the wait
   * grow or frames come late for good. */
  sim_params fast = {.pc_fps = 60, .pc_ppm = -200, .transit_us = 6000, .jitter_us = 2000, .seconds = 120};
  r = simulate(&fast);
  print("PC clock 200 ppm fast", &r);
  CHECK(r.lat_max_late <= r.lat_max_early + 4000, "wait does not grow with a fast PC clock");
  CHECK(r.dropped <= r.frames / 200, "few frames dropped with a fast PC clock");
  CHECK(r.idle_late <= r.frames / 200, "fast PC clock: few repeated frames");
  sim_params slow = {.pc_fps = 60, .pc_ppm = 200, .transit_us = 6000, .jitter_us = 2000, .seconds = 120};
  r = simulate(&slow);
  print("PC clock 200 ppm slow", &r);
  CHECK(r.lat_max_late <= PACE_DELAY_MAX_US + 2 * VITA_PERIOD_US, "wait bounded with a slow PC clock");
  CHECK(r.dropped <= r.frames / 200, "few frames dropped with a slow PC clock");
  CHECK(r.idle_late <= r.frames / 200, "slow PC clock: few repeated frames");

  /* An extreme 0.1 % either way: still no runaway wait, few drops. */
  sim_params xfast = {.pc_fps = 60, .pc_ppm = -1000, .transit_us = 6000, .jitter_us = 2000, .seconds = 120};
  r = simulate(&xfast);
  print("PC clock 0.1 % fast", &r);
  CHECK(r.lat_max_late <= r.lat_max_early + 6000, "0.1 % fast: wait does not grow");
  CHECK(r.dropped <= r.frames / 50, "0.1 % fast: under 2 % dropped");
  sim_params xslow = {.pc_fps = 60, .pc_ppm = 1000, .transit_us = 6000, .jitter_us = 2000, .seconds = 120};
  r = simulate(&xslow);
  print("PC clock 0.1 % slow", &r);
  CHECK(r.lat_max_late <= PACE_DELAY_MAX_US + 2 * VITA_PERIOD_US, "0.1 % slow: wait bounded");
  CHECK(r.dropped <= r.frames / 50, "0.1 % slow: under 2 % dropped");

  /* The refresh rate of the Vita matched by the PC (setting "Match the
   * screen's refresh rate": 59.94 fps): nothing to drop. */
  sim_params matched = {.pc_fps = 59.94, .transit_us = 6000, .jitter_us = 3000, .seconds = 60};
  r = simulate(&matched);
  print("59.94 fps (matched)", &r);
  CHECK(r.dropped <= 2 && r.idle_late <= 2, "matched rate: no drop, no repeat");

  /* 30 fps: each frame shown once, on every other refresh. */
  sim_params half = {.pc_fps = 30, .transit_us = 8000, .jitter_us = 4000, .seconds = 60};
  r = simulate(&half);
  print("30 fps", &r);
  CHECK(r.out_of_order == 0, "frames shown in order");
  CHECK(r.dropped <= r.frames / 200, "30 fps: nearly nothing dropped");

  /* The PC stops sending for 3 s (the game paused, a loading screen): the
   * first frame after must not wait for a clock that has run on. */
  sim_params pause = {.pc_fps = 60, .transit_us = 6000, .jitter_us = 3000, .seconds = 30,
                      .pause_at_s = 10, .pause_len_s = 3};
  r = simulate(&pause);
  print("3 s pause on the PC", &r);
  CHECK(r.frames_after_pause_wait <= 3, "first frame after a pause shown within 3 refreshes");
  CHECK(r.never <= 2, "every frame shown or dropped after a pause");

  /* Catch-up: the newest ready frame is shown at once and becomes the anchor. */
  pace_state p;
  pace_reset(&p);
  pace_frame fr[3] = {{true, 1000000, 1}, {true, 1016667, 2}, {true, 1033333, 3}};
  uint32_t drop = 0;
  int pick = pace_pick(&p, fr, 3, 50000000, true, &drop);
  CHECK(pick == 2, "catch-up picks the newest frame");
  CHECK(drop == 0x3, "catch-up drops the older frames");
  CHECK(p.valid && p.anchor_pts == 1033333 && p.anchor_local == 50000000, "catch-up anchors on the frame shown");

  /* The PC's clock going back (a new stream) re-anchors instead of waiting. */
  pace_reset(&p);
  uint64_t t1 = pace_target(&p, 9000000, 100);
  uint64_t t2 = pace_target(&p, 1000, 200);
  CHECK(t1 == 100 + PACE_DELAY_START_US, "first frame due after the start delay");
  CHECK(t2 == 200 + PACE_DELAY_START_US, "clock going back re-anchors");

  return TEST_END();
}
