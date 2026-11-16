#ifndef SPLASH_LOGIC_H
#define SPLASH_LOGIC_H

/* Start-up screen: when things happen and where they are drawn. Kept free of
 * Vita calls: tested on a PC (tools/tests/test_splash.c). The drawing is in
 * splash.c.
 *
 * While VitaMoon loads, the moon of assets/moon.png waxes with the loading,
 * from the new moon (picture 0) to the last phase before the full moon
 * (MOON_PHASES - 2). The full moon is the VitaMoon mark: it is reached only
 * once everything is loaded. It stays a moment, then the mark and the name
 * move to their places in the top bar of Home, which takes over from there.
 * Everything follows real time, never a count of frames. */

#include <stdbool.h>
#include <stdint.h>

#include "moon.h"

/* Where the moon and the name are while VitaMoon loads (960x544 screen). */
#define SPLASH_MOON_CX    480.0f   /* the moon's centre */
#define SPLASH_MOON_CY    226.0f
#define SPLASH_NAME_BASE  330.0f   /* the name's baseline, centred under it */

#define SPLASH_STEP_US     50000   /* least time per picture while loading */
#define SPLASH_CATCHUP_US  28000   /* per picture once everything is loaded */
#define SPLASH_NAME_US    360000   /* the name fades in once its font is ready */
#define SPLASH_NAME_RISE      6.0f /* ... rising this many pixels */
#define SPLASH_HOLD_US    160000   /* the full mark stays before it moves */
#define SPLASH_GLIDE_US   480000   /* the mark and the name go to the top bar */
#define SPLASH_NAME_LAG     0.20f  /* part of it the name waits: the mark leads, the name never covers it */
#define SPLASH_EARTHSHINE  0.10f   /* the unlit part of the moon, faintly visible */

typedef struct {
  float shown;        /* picture shown, fractional between two (crossfade) */
  bool started;       /* last_us is set */
  uint64_t last_us;   /* time of the previous update */
  uint64_t full_us;   /* when the mark was reached (0: not yet) */
  bool full;
} splash_moon;

/* Picture to reach for a loading progress in [0, 1]: the mark only once loaded. */
static inline float splash_target(float progress, bool loaded) {
  if (loaded)
    return (float)(MOON_PHASES - 1);
  if (!(progress > 0.0f))      /* also NaN */
    progress = 0.0f;
  if (progress > 1.0f)
    progress = 1.0f;
  return progress * (float)(MOON_PHASES - 2);
}

/* Moves the picture shown towards target: never back, never faster than one
 * picture per SPLASH_STEP_US (SPLASH_CATCHUP_US once loaded), and at most one
 * picture per update, so that a long wait between two frames does not make
 * the moon jump over its phases. */
static inline void splash_moon_update(splash_moon *m, float target, bool loaded, uint64_t now_us) {
  if (!m->started || now_us < m->last_us) {
    m->started = true;
    m->last_us = now_us;
  }
  float step_us = loaded ? (float)SPLASH_CATCHUP_US : (float)SPLASH_STEP_US;
  float can = (float)(now_us - m->last_us) / step_us;
  if (can > 1.0f)
    can = 1.0f;
  m->last_us = now_us;
  if (target > m->shown) {
    m->shown += can;
    if (m->shown > target)
      m->shown = target;
  }
  if (!m->full && m->shown >= (float)(MOON_PHASES - 1)) {
    m->shown = (float)(MOON_PHASES - 1);
    m->full = true;
    m->full_us = now_us;
  }
}

/* The two pictures drawn for a fractional picture: `base` fully, then `over`
 * (-1: none) with over_alpha on top. While the moon waxes each picture holds
 * the lit part of the one before, so the next one fades in over it. The mark
 * has a V cut out of it: it goes under, and the last phase fades off it. */
typedef struct {
  int base;
  int over;
  float over_alpha;
} splash_pictures;

static inline splash_pictures splash_pictures_for(float shown) {
  splash_pictures p = {0, -1, 0.0f};
  if (!(shown > 0.0f))
    return p;
  if (shown >= (float)(MOON_PHASES - 1)) {
    p.base = MOON_PHASES - 1;
    return p;
  }
  int i = (int)shown;
  float f = shown - (float)i;
  p.base = i;
  if (f <= 0.0f)
    return p;
  if (i + 1 == MOON_PHASES - 1) {
    p.base = MOON_PHASES - 1;
    p.over = i;
    p.over_alpha = 1.0f - f;
  } else {
    p.over = i + 1;
    p.over_alpha = f;
  }
  return p;
}

/* Opacity of the faint full disc behind the phases: gone with the mark, so
 * that its V shows the sky. */
static inline float splash_earthshine(float shown) {
  float last = (float)(MOON_PHASES - 2);
  if (shown <= last)
    return SPLASH_EARTHSHINE;
  float left = 1.0f - (shown - last);
  return left > 0.0f ? SPLASH_EARTHSHINE * left : 0.0f;
}

/* Smooth start and end. */
static inline float splash_ease(float t) {
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  if (t < 0.5f)
    return 4.0f * t * t * t;
  float u = 1.0f - t;
  return 1.0f - 4.0f * u * u * u;
}

/* Fast start, soft end. */
static inline float splash_ease_out(float t) {
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  float u = 1.0f - t;
  return 1.0f - u * u * u;
}

static inline float splash_lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

/* Time through the move to the top bar, 0 to 1 (not eased). */
static inline float splash_glide_time(const splash_moon *m, uint64_t now_us) {
  if (!m->full || now_us < m->full_us + SPLASH_HOLD_US)
    return 0.0f;
  float t = (float)(now_us - m->full_us - SPLASH_HOLD_US) / (float)SPLASH_GLIDE_US;
  return t > 1.0f ? 1.0f : t;
}

/* How far the mark has gone to the top bar, 0 to 1, eased. */
static inline float splash_glide(const splash_moon *m, uint64_t now_us) {
  return splash_ease(splash_glide_time(m, now_us));
}

/* The same for the name, which starts a little later and arrives with the mark. */
static inline float splash_name_glide(float glide_time) {
  return splash_ease((glide_time - SPLASH_NAME_LAG) / (1.0f - SPLASH_NAME_LAG));
}

/* Home can take over: the mark and the name are in the top bar. */
static inline bool splash_finished(const splash_moon *m, uint64_t now_us) {
  return m->full && now_us >= m->full_us + SPLASH_HOLD_US + SPLASH_GLIDE_US;
}

/* Opacity of the name, from the time its font was ready (0: not yet). */
static inline float splash_name_alpha(uint64_t ready_us, uint64_t now_us) {
  if (ready_us == 0 || now_us < ready_us)
    return 0.0f;
  return splash_ease_out((float)(now_us - ready_us) / (float)SPLASH_NAME_US);
}

/* Where a picture of `cell` pixels is drawn: its top-left corner and scale.
 * At glide 0 it is centred on (cx, cy) at its own size; at glide 1 it covers
 * the top bar's mark, a `mark_size` square at (mark_x, mark_y). */
typedef struct {
  float x, y, scale;
} splash_place;

static inline splash_place splash_moon_place(float glide, float cell, float cx, float cy,
                                             float mark_x, float mark_y, float mark_size) {
  splash_place p;
  p.scale = splash_lerp(1.0f, mark_size / cell, glide);
  p.x = splash_lerp(cx - cell / 2.0f, mark_x, glide);
  p.y = splash_lerp(cy - cell / 2.0f, mark_y, glide);
  return p;
}

/* Where the name is drawn: left edge, baseline and size. At glide 0 it is
 * centred on cx at size `size0` (width0 wide); at glide 1 it is where the top
 * bar draws it. */
static inline splash_place splash_name_place(float glide, float cx, float base0, float size0, float width0,
                                             float bar_x, float bar_base, float bar_size) {
  splash_place p;
  p.scale = splash_lerp(size0, bar_size, glide);     /* the text size */
  p.x = splash_lerp(cx - width0 / 2.0f, bar_x, glide);
  p.y = splash_lerp(base0, bar_base, glide);
  return p;
}

/* Progress of the loading, 0 to 1, from the weights of the steps done and the
 * part done of the current one. */
static inline float splash_progress(int done_weight, int current_weight, float current_part, int total_weight) {
  if (total_weight <= 0)
    return 1.0f;
  if (current_part < 0.0f)
    current_part = 0.0f;
  if (current_part > 1.0f)
    current_part = 1.0f;
  float p = ((float)done_weight + (float)current_weight * current_part) / (float)total_weight;
  return p > 1.0f ? 1.0f : (p < 0.0f ? 0.0f : p);
}

#endif // SPLASH_LOGIC_H
