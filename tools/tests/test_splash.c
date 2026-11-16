/*
 * Start-up screen: the timeline of src/gui/splash_logic.h (which picture of
 * the moon is shown while VitaMoon loads, when it becomes the mark, where the
 * mark and the name go) against the top bar's real layout (theme.h).
 */
#include "test.h"
#include "splash_logic.h"
#include "theme.h"

#include <math.h>
#include <string.h>

/* The lit disc inside a picture of assets/moon.png (its last picture, the
 * mark): x 17..94, y 31..97 of 112 (measured on the file). */
#define DISC_X0 (17.0f / MOON_CELL)
#define DISC_X1 (94.0f / MOON_CELL)
#define DISC_Y0 (31.0f / MOON_CELL)
#define DISC_Y1 (97.0f / MOON_CELL)
/* "VitaMoon" in Poppins at 34 px is 160 px wide; capitals are about 0.74 of
 * the size above the baseline. Widths around it are tried too. */
#define NAME_WIDTH_34 160.0f

static bool near(float a, float b) {
  return fabsf(a - b) < 0.001f;
}

/* Smallest gap between the mark's disc and the name's box during the move
 * (negative: they overlap), for a name `width0` wide at TS_HERO. */
static float closest_gap(float width0) {
  float worst = 1e9f;
  for (int i = 0; i <= 2000; i++) {
    float t = i / 2000.0f;
    float g = splash_ease(t), gn = splash_name_glide(t);
    splash_place m = splash_moon_place(g, MOON_CELL, SPLASH_MOON_CX, SPLASH_MOON_CY, TH_MARK_X, TH_MARK_Y,
                                       TH_MARK_SIZE);
    float side = MOON_CELL * m.scale;
    float dx0 = m.x + DISC_X0 * side, dx1 = m.x + DISC_X1 * side;
    float dy0 = m.y + DISC_Y0 * side, dy1 = m.y + DISC_Y1 * side;
    splash_place n = splash_name_place(gn, SPLASH_MOON_CX, SPLASH_NAME_BASE, TS_HERO, width0, TH_NAME_X,
                                       TH_TOPBAR_BASE, TS_LEAD);
    float nx0 = n.x, nx1 = n.x + width0 * n.scale / TS_HERO;
    float ny0 = n.y - 0.74f * n.scale, ny1 = n.y + 0.05f * n.scale;
    float gap_x = fmaxf(nx0 - dx1, dx0 - nx1);
    float gap_y = fmaxf(ny0 - dy1, dy0 - ny1);
    float gap = fmaxf(gap_x, gap_y);
    if (gap < worst)
      worst = gap;
  }
  return worst;
}

int main(void) {
  TEST_BEGIN("start-up screen");

  /* which picture to reach */
  CHECK(splash_target(0.0f, false) == 0.0f, "nothing loaded: the new moon");
  CHECK(splash_target(1.0f, false) == MOON_PHASES - 2, "all steps done but not loaded: the last phase, not the mark");
  CHECK(splash_target(0.5f, true) == MOON_PHASES - 1, "loaded: the mark");
  CHECK(splash_target(NAN, false) == 0.0f && splash_target(-1.0f, false) == 0.0f, "nonsense progress: the new moon");
  CHECK(splash_target(7.0f, false) == MOON_PHASES - 2, "progress over 1 is held at the last phase");

  /* the picture shown */
  splash_moon m;
  memset(&m, 0, sizeof(m));
  splash_moon_update(&m, 5.0f, false, 1000000);
  CHECK(m.shown == 0.0f, "the first update only starts the clock");
  splash_moon_update(&m, 5.0f, false, 1000000 + SPLASH_STEP_US);
  CHECK(near(m.shown, 1.0f), "one picture per SPLASH_STEP_US while loading");
  splash_moon_update(&m, 5.0f, false, 1000000 + 10 * SPLASH_STEP_US);
  CHECK(near(m.shown, 2.0f), "a long wait between two frames: still one picture, no jump");
  splash_moon_update(&m, 1.0f, false, 1000000 + 11 * SPLASH_STEP_US);
  CHECK(near(m.shown, 2.0f), "never goes back");
  for (uint64_t t = 1000000 + 12 * SPLASH_STEP_US; t < 4000000; t += 16683)
    splash_moon_update(&m, splash_target(1.0f, false), false, t);
  CHECK(near(m.shown, MOON_PHASES - 2) && !m.full, "steps done: the last phase, no mark before the end");
  uint64_t t = 4000000;
  int frames = 0;
  while (!m.full && frames < 1000) {
    t += 16683;
    splash_moon_update(&m, splash_target(1.0f, true), true, t);
    frames++;
  }
  CHECK(m.full && m.shown == MOON_PHASES - 1 && m.full_us == t, "loaded: the mark, and when it came");
  CHECK(frames <= 3, "from the last phase to the mark: a few frames");
  splash_moon_update(&m, splash_target(1.0f, true), true, t + 1000000);
  CHECK(m.shown == MOON_PHASES - 1, "the mark stays");

  /* from the new moon to the mark when everything loads at once */
  memset(&m, 0, sizeof(m));
  t = 0;
  splash_moon_update(&m, splash_target(1.0f, true), true, t);
  frames = 0;
  while (!m.full && frames < 1000) {
    t += 16683;
    splash_moon_update(&m, splash_target(1.0f, true), true, t);
    frames++;
  }
  uint64_t expected = (uint64_t)(MOON_PHASES - 1) * SPLASH_CATCHUP_US;
  CHECK(t + 16683 >= expected && t <= expected + 16683,
        "everything loaded at once: still every phase, one per SPLASH_CATCHUP_US (a third of a second)");

  /* pictures drawn */
  splash_pictures p = splash_pictures_for(0.0f);
  CHECK(p.base == 0 && p.over == -1, "0: the new moon alone");
  p = splash_pictures_for(3.0f);
  CHECK(p.base == 3 && p.over == -1, "a whole picture alone");
  p = splash_pictures_for(3.25f);
  CHECK(p.base == 3 && p.over == 4 && near(p.over_alpha, 0.25f), "between two phases: the next fades in over it");
  p = splash_pictures_for(MOON_PHASES - 2 + 0.25f);
  CHECK(p.base == MOON_PHASES - 1 && p.over == MOON_PHASES - 2 && near(p.over_alpha, 0.75f),
        "towards the mark: the mark under, the last phase fading off it");
  p = splash_pictures_for(MOON_PHASES - 1);
  CHECK(p.base == MOON_PHASES - 1 && p.over == -1, "the mark alone");
  CHECK(splash_pictures_for(NAN).base == 0, "nonsense: the new moon");

  CHECK(splash_earthshine(0.0f) == SPLASH_EARTHSHINE, "the unlit part shows faintly while loading");
  CHECK(near(splash_earthshine(MOON_PHASES - 1.5f), SPLASH_EARTHSHINE / 2), "it fades as the mark comes");
  CHECK(splash_earthshine(MOON_PHASES - 1) == 0.0f, "the mark's V shows the sky");

  /* easing */
  bool rising = true;
  for (int i = 1; i <= 100; i++)
    rising = rising && splash_ease(i / 100.0f) >= splash_ease((i - 1) / 100.0f);
  CHECK(splash_ease(0) == 0 && splash_ease(1) == 1 && near(splash_ease(0.5f), 0.5f) && rising,
        "ease: from 0 to 1, never back");
  CHECK(splash_ease(-1) == 0 && splash_ease(2) == 1, "ease: held outside 0..1");

  /* the move to the top bar */
  memset(&m, 0, sizeof(m));
  m.full = true;
  m.full_us = 5000000;
  CHECK(splash_glide(&m, 5000000 + SPLASH_HOLD_US - 1) == 0.0f, "the mark stays a moment before it moves");
  CHECK(!splash_finished(&m, 5000000 + SPLASH_HOLD_US + SPLASH_GLIDE_US - 1), "not finished during the move");
  CHECK(splash_finished(&m, 5000000 + SPLASH_HOLD_US + SPLASH_GLIDE_US), "finished at its end");
  CHECK(splash_glide(&m, 5000000 + SPLASH_HOLD_US + SPLASH_GLIDE_US) == 1.0f &&
            splash_name_glide(splash_glide_time(&m, 9000000)) == 1.0f,
        "at the end the mark and the name are all the way");
  memset(&m, 0, sizeof(m));
  CHECK(splash_glide(&m, 9000000) == 0.0f && !splash_finished(&m, 9000000), "no move before the mark");
  CHECK(splash_name_glide(SPLASH_NAME_LAG) == 0.0f, "the name starts after the mark");

  splash_place mp = splash_moon_place(0.0f, MOON_CELL, SPLASH_MOON_CX, SPLASH_MOON_CY, TH_MARK_X, TH_MARK_Y,
                                      TH_MARK_SIZE);
  CHECK(near(mp.x + MOON_CELL / 2.0f, SPLASH_MOON_CX) && near(mp.y + MOON_CELL / 2.0f, SPLASH_MOON_CY) &&
            mp.scale == 1.0f,
        "loading: the moon centred, at its own size");
  mp = splash_moon_place(1.0f, MOON_CELL, SPLASH_MOON_CX, SPLASH_MOON_CY, TH_MARK_X, TH_MARK_Y, TH_MARK_SIZE);
  CHECK(near(mp.x, TH_MARK_X) && near(mp.y, TH_MARK_Y) && near(MOON_CELL * mp.scale, TH_MARK_SIZE),
        "at the end: exactly over the top bar's mark");
  splash_place np = splash_name_place(1.0f, SPLASH_MOON_CX, SPLASH_NAME_BASE, TS_HERO, NAME_WIDTH_34, TH_NAME_X,
                                      TH_TOPBAR_BASE, TS_LEAD);
  CHECK(near(np.x, TH_NAME_X) && near(np.y, TH_TOPBAR_BASE) && near(np.scale, TS_LEAD),
        "at the end: the name where the top bar draws it, at its size");
  np = splash_name_place(0.0f, SPLASH_MOON_CX, SPLASH_NAME_BASE, TS_HERO, NAME_WIDTH_34, TH_NAME_X, TH_TOPBAR_BASE,
                         TS_LEAD);
  CHECK(near(np.x + NAME_WIDTH_34 / 2, SPLASH_MOON_CX) && np.y > SPLASH_MOON_CY + MOON_CELL * (DISC_Y1 - 0.5f),
        "loading: the name centred under the moon");

  float gap = closest_gap(NAME_WIDTH_34);
  char what[128];
  snprintf(what, sizeof(what), "the name never touches the mark on the way (closest: %.1f px)", gap);
  CHECK(gap >= 4.0f, what);
  CHECK(closest_gap(NAME_WIDTH_34 * 0.75f) >= 4.0f && closest_gap(NAME_WIDTH_34 * 1.3f) >= 4.0f,
        "... nor with a narrower or wider name");

  /* progress */
  CHECK(splash_progress(0, 0, 0, 20) == 0.0f && splash_progress(20, 0, 0, 20) == 1.0f, "progress: 0 to 1");
  CHECK(near(splash_progress(5, 10, 0.5f, 20), 0.5f), "progress: a step half done counts half");
  CHECK(splash_progress(5, 10, 3.0f, 20) <= 1.0f && splash_progress(1, 0, 0, 0) == 1.0f, "progress: held in 0..1");

  return TEST_END();
}
