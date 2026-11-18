/*
 * Library and menus: when a game was last played (src/gui/library_logic.h),
 * the order of the shelf, the twinkling of the stars (src/gui/twinkle.h) and
 * the trackpad's tap (src/input/tap.h).
 */
#include "test.h"
#include "library_logic.h"
#include "twinkle.h"
#include "tap.h"
#include "orbit.h"
#include "stars.h"

#define THEME_COUNT_FOR_TEST ((int)(sizeof(k_theme_orbit_count) / sizeof(k_theme_orbit_count[0])))

#include <math.h>
#include <stdlib.h>
#include <string.h>

static lib_moment at(int y, int m, int d, int h, int min) {
  lib_moment x = {y, m, d, h, min};
  return x;
}

int main(void) {
  TEST_BEGIN("library, stars and tap");

  /* reading the moment written by stream_stats.c */
  lib_moment m;
  CHECK(lib_parse_moment("2026-11-16 19:46", &m) && m.year == 2026 && m.month == 11 && m.day == 16 &&
            m.hour == 19 && m.minute == 46,
        "a last-session moment is read");
  CHECK(!lib_parse_moment("", &m) && !lib_parse_moment(NULL, &m) && !lib_parse_moment("2026-13-01 10:00", &m) &&
            !lib_parse_moment("2026-11-16", &m) && !lib_parse_moment("garbage", &m),
        "empty, missing, impossible or cut moments are refused");

  /* days between dates */
  CHECK(lib_days(1970, 1, 1) == 0 && lib_days(2026, 11, 16) == 20773, "days since 1970 (2026-11-16 is day 20773, as Python counts)");
  CHECK(lib_days(2024, 3, 1) - lib_days(2024, 2, 28) == 2, "leap year: 29 February counts");
  CHECK(lib_days(2027, 1, 1) - lib_days(2026, 12, 31) == 1, "across a new year");

  lib_moment today = at(2026, 11, 16, 9, 5);
  int days = -1;
  lib_moment t = at(2026, 11, 16, 0, 10);
  CHECK(lib_when(&t, &today, &days) == LIB_WHEN_TODAY && days == 0, "same day: today");
  t = at(2026, 11, 15, 23, 59);
  CHECK(lib_when(&t, &today, &days) == LIB_WHEN_YESTERDAY && days == 1, "the day before, even late: yesterday");
  t = at(2026, 11, 14, 10, 0);
  CHECK(lib_when(&t, &today, &days) == LIB_WHEN_DAYS_AGO && days == 2, "two days ago");
  t = at(2026, 11, 10, 10, 0);
  CHECK(lib_when(&t, &today, &days) == LIB_WHEN_DAYS_AGO && days == 6, "six days ago");
  t = at(2026, 11, 9, 10, 0);
  CHECK(lib_when(&t, &today, &days) == LIB_WHEN_DATE, "a week or more: the date");
  t = at(2026, 11, 17, 10, 0);
  CHECK(lib_when(&t, &today, &days) == LIB_WHEN_DATE, "a moment after today (clock set back): the date");
  lib_moment jan1 = at(2027, 1, 1, 8, 0);
  t = at(2026, 12, 31, 22, 0);
  CHECK(lib_when(&t, &jan1, &days) == LIB_WHEN_YESTERDAY, "31 December seen on 1 January: yesterday");

  unsigned int hours, minutes;
  lib_duration_parts(390, &hours, &minutes);
  CHECK(hours == 0 && minutes == 7, "6 min 30 s: 7 min (a started minute counts)");
  lib_duration_parts(72 * 60, &hours, &minutes);
  CHECK(hours == 1 && minutes == 12, "72 min: 1 h 12");
  lib_duration_parts(0, &hours, &minutes);
  CHECK(hours == 0 && minutes == 1, "never less than 1 min");

  /* the shelf's order */
  lib_sort_key never_first = {false, "", 0}, never_last = {false, "", 7};
  lib_sort_key yesterday = {false, "2026-11-15 21:10", 5}, today_k = {false, "2026-11-16 08:00", 3};
  lib_sort_key fav_never = {true, "", 6}, fav_old = {true, "2026-01-01 10:00", 4};
  CHECK(lib_compare(&today_k, &yesterday) < 0, "played later comes first");
  CHECK(lib_compare(&yesterday, &never_first) < 0, "played comes before never played");
  CHECK(lib_compare(&never_first, &never_last) < 0, "never played: the PC's order");
  CHECK(lib_compare(&fav_never, &today_k) < 0, "a favourite comes first, even never played");
  CHECK(lib_compare(&fav_old, &fav_never) < 0, "among favourites: the most recently played first");
  CHECK(lib_compare(&yesterday, &yesterday) == 0, "the same game is equal to itself");
  lib_sort_key no_when = {false, NULL, 2};
  CHECK(lib_compare(&no_when, &never_last) < 0 && lib_compare(&yesterday, &no_when) < 0,
        "a missing moment counts as never played");

  /* twinkling stars */
  bool bounded = true, smooth = true, some_rest = false, some_peak = false;
  float worst_step = 0.0f;
  int active = 0, samples = 0;
  for (uint32_t star = 0; star < 80; star++) {
    float prev = twinkle_level(star, 0);
    for (uint64_t t = 16683; t < 30000000; t += 16683) {
      float l = twinkle_level(star, t);
      bounded = bounded && l >= 0.0f && l <= 1.0f;
      float step = l > prev ? l - prev : prev - l;
      if (step > worst_step)
        worst_step = step;
      some_rest = some_rest || l == 0.0f;
      some_peak = some_peak || l > 0.98f;
      active += l > 0.0f;
      samples++;
      prev = l;
    }
  }
  smooth = worst_step < 0.08f;
  char what[128];
  CHECK(bounded && some_rest && some_peak, "each star rests and reaches its peak, within 0..1");
  snprintf(what, sizeof(what), "no flicker: at most %.3f from one frame to the next", worst_step);
  CHECK(smooth, what);
  float share = (float)active / samples;
  snprintf(what, sizeof(what), "a star brightens %.0f %% of the time", share * 100);
  CHECK(share > 0.35f && share < 0.5f, what);
  bool differ = false;
  for (uint64_t t = 0; t < 3000000 && !differ; t += 100000)
    differ = twinkle_level(0, t) != twinkle_level(1, t);
  CHECK(differ, "stars twinkle at their own moments");
  float max_alpha = 0.0f;
  for (uint64_t t = 0; t < 8000000; t += 16683) {
    float a = twinkle_alpha(3, 500.0f, t);   /* brighter than any star: held at the peak */
    if (a > max_alpha)
      max_alpha = a;
  }
  CHECK(max_alpha <= TWINKLE_PEAK + 1e-6f && max_alpha > TWINKLE_PEAK * 0.9f, "the white dot never exceeds its peak");
  CHECK(twinkle_alpha(3, 0.0f, 1000000) == 0.0f && twinkle_alpha(3, -5.0f, 1000000) == 0.0f,
        "a star of no brightness does not twinkle");

  /* trackpad tap */
  CHECK(tap_judge(80000, 250, 2, -3, true) == TAP_CLICK, "lifted at 80 ms without moving: a click");
  CHECK(tap_judge(220000, 250, 0, 0, true) == TAP_CLICK, "lifted at 220 ms with 250 ms set: still a click");
  CHECK(tap_judge(220000, 100, 0, 0, true) == TAP_SWIPE, "the same with 100 ms set: not a click");
  CHECK(tap_judge(150000, 250, 0, 0, false) == TAP_UNDECIDED, "still down, still and short: wait");
  CHECK(tap_judge(30000, 250, TAP_MOVE_PX + 1, 0, false) == TAP_SWIPE,
        "moving past the threshold starts the swipe at once, before the tap time");
  CHECK(tap_judge(30000, 250, 0, -(TAP_MOVE_PX + 1), true) == TAP_SWIPE, "a quick flick is not a click");
  CHECK(tap_judge(30000, 250, TAP_MOVE_PX, TAP_MOVE_PX, true) == TAP_CLICK, "a small wobble still clicks");
  CHECK(tap_judge(250000, 250, 0, 0, false) == TAP_SWIPE, "held past the tap time: the pointer follows");

  /* moving sky */
  {
    float worst = 0.0f;
    bool falling = true;
    float last = 2.0f;
    for (int y = 0; y <= 544; y++) {
      float k = orbit_keep((float)y);
      float exact = powf(1.0f - y / 544.0f, 1.4f);
      if (fabsf(k - exact) > worst)
        worst = fabsf(k - exact);
      if (k > last + 1e-6f)
        falling = false;
      last = k;
    }
    snprintf(what, sizeof(what), "the horizon's share of stars: within %.4f of (1 - y/H)^1.4", worst);
    CHECK(worst < 0.007f, what);
    CHECK(falling && fabsf(orbit_keep(0.0f) - 1.0f) < 1e-4f && orbit_keep(544.0f) < 1e-6f,
          "fewer stars lower down: all at the top, none at the bottom");
  }
  {
    /* speeds: still, then each twice the one before; the angle only moves on */
    bool doubling = k_orbit_turn_s[0] == 0;
    for (int v = 2; v < ORBIT_SPEEDS; v++)
      doubling = doubling && k_orbit_turn_s[v - 1] == 2 * k_orbit_turn_s[v];
    CHECK(doubling && k_orbit_turn_s[1] == 32 * 60 && k_orbit_turn_s[ORBIT_SPEEDS - 1] == 4 * 60,
          "four speeds: one turn in 32, 16, 8 and 4 minutes");
    CHECK(k_orbit_turn_s[3] == 8 * 60, "speed 3 (SKY_MEDIUM, the default in config.h): one turn in 8 minutes");
    CHECK(orbit_advance(1.0, 16683, 0, false) == 1.0 && orbit_advance(1.0, 16683, 9, false) == 1.0,
          "still (or unknown speed): the sky does not move");
    double a = 0.0;
    for (int f = 0; f < 16 * 60 * 4; f++)   /* 16 minutes in 0.25 s steps */
      a = orbit_advance(a, 250000, 2, false);
    CHECK(fabs(a) < 1e-6 || fabs(a - ORBIT_TWO_PI) < 1e-6, "Slow: one whole turn in 16 minutes");
    double cw = orbit_advance(0.5, 1000000, 3, false), acw = orbit_advance(0.5, 1000000, 3, true);
    CHECK(fabs((cw - 0.5) + (acw - 0.5)) < 1e-9 && cw > 0.5, "anticlockwise: the same step the other way");
    CHECK(orbit_advance(0.001, 1000000, 4, true) > 6.0, "the angle stays within one turn, either way");
    CHECK(orbit_advance(0.0, 3600000000ull, 4, false) == orbit_advance(0.0, ORBIT_MAX_STEP_US, 4, false),
          "back from an hour of streaming: the sky goes on from where it was, no jump");
    double before = orbit_advance(2.0, 16683, 1, false), after = orbit_advance(before, 16683, 4, false);
    CHECK(after - before < 0.0005 && after > before,
          "changing the speed: the stars go on from where they are (no jump)");
    float x, y;
    double turned = 0.0;
    for (int f = 0; f < 120; f++)
      turned = orbit_advance(turned, 16683, 4, false);
    orbit_place(500.0f, 500.0f, -300.0f, 0.0f, cosf((float)turned), sinf((float)turned), &x, &y);
    bool left_rises = y < 500.0f;
    orbit_place(500.0f, 500.0f, 0.0f, -300.0f, cosf((float)turned), sinf((float)turned), &x, &y);
    CHECK(left_rises && x > 500.0f, "clockwise: a star left of the moon rises, one above it goes right");
  }
  {
    float far = 0.0f, near = 1e9f;
    for (int t = 0; t < THEME_COUNT_FOR_TEST; t++)
      for (int i = 0; i < k_theme_orbit_count[t]; i++) {
        float r = hypotf(k_theme_orbit[t][i].dx, k_theme_orbit[t][i].dy);
        if (r > far) far = r;
        if (r < near) near = r;
      }
    CHECK(near >= 319.9f, "no star crosses the moon (all at least 20 px outside it)");
    CHECK(far >= hypotf(ORBIT_CX, ORBIT_CY), "the ring reaches the far corner: no empty patch as it turns");
    float slow = 6.28318531f * far / k_orbit_turn_s[1], fast = 6.28318531f * far / k_orbit_turn_s[ORBIT_SPEEDS - 1];
    snprintf(what, sizeof(what), "at the far corner: %.1f px/s (very slow) to %.1f px/s (fast), %.2f px a frame at most",
             slow, fast, fast / 60.0f);
    CHECK(slow < 4.0f && fast < 30.0f, what);
  }
  CHECK(orbit_top_bar(100.0f, 30.0f) == 0.0f && orbit_top_bar(800.0f, 20.0f) == 0.0f &&
            orbit_top_bar(480.0f, 30.0f) == 1.0f && orbit_top_bar(100.0f, 80.0f) == 1.0f,
        "the top bar's text stays clear, the middle of the top does not");
  CHECK(orbit_light(-20.0f, 100.0f, 100.0f, 0.1f) == 0.0f && orbit_light(500.0f, 600.0f, 100.0f, 0.0f) == 0.0f,
        "off the screen: not drawn");
  CHECK(fabsf(orbit_alpha(0.55f, 100.0f) * orbit_spread(0.55f) * orbit_spread(0.55f) -
              100.0f / 255.0f * 0.55f * 0.55f) < 1e-6f && orbit_alpha(1.2f, 100.0f) == 100.0f / 255.0f,
        "a small star drawn wider gives the same light");
  {
    /* At every moment the sky looks like the still picture: about as many
     * stars on the screen, as bright, and none popping in or out. */
    bool like_still = true, smooth = true;
    float worst_ratio = 1.0f, worst_step = 0.0f;
    for (int t = 0; t < THEME_COUNT_FOR_TEST; t++) {
      float still = 0.0f;
      for (int i = 0; i < k_theme_star_count[t]; i++)
        still += k_theme_stars[t][i].bright;
      for (int k = 0; k < 16; k++) {
        /* one frame at the fastest speed: the largest change between frames */
        float a0 = 6.28318531f * k / 16.0f;
        float a1 = (float)orbit_advance(a0, 16683, ORBIT_SPEEDS - 1, false);
        float shown = 0.0f;
        for (int i = 0; i < k_theme_orbit_count[t]; i++) {
          const orbit_star *o = &k_theme_orbit[t][i];
          float x, y, x1, y1;
          orbit_place(ORBIT_CX, ORBIT_CY, o->dx, o->dy, cosf(a0), sinf(a0), &x, &y);
          orbit_place(ORBIT_CX, ORBIT_CY, o->dx, o->dy, cosf(a1), sinf(a1), &x1, &y1);
          float l0 = orbit_light(x, y, o->bright, o->depth), l1 = orbit_light(x1, y1, o->bright, o->depth);
          /* a star crossing the screen's edge goes off as a whole: it is
           * already outside by more than its own size there */
          if (fabsf(l1 - l0) > worst_step && (l0 > 0.0f) == (l1 > 0.0f))
            worst_step = fabsf(l1 - l0);
          shown += l0;
        }
        float ratio = shown / still;
        if (fabsf(ratio - 1.0f) > fabsf(worst_ratio - 1.0f))
          worst_ratio = ratio;
        if (ratio < 0.8f || ratio > 1.25f)
          like_still = false;
      }
    }
    snprintf(what, sizeof(what), "the turning sky's light stays near the still one's (worst %.2f times)", worst_ratio);
    CHECK(like_still, what);
    snprintf(what, sizeof(what), "no star pops in or out: %.2f of 115 at most from one frame to the next", worst_step);
    smooth = worst_step < 3.0f;
    CHECK(smooth, what);
  }

  return TEST_END();
}
