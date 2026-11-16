#ifndef ORBIT_H
#define ORBIT_H

/* Moving sky ("Moving sky" in Settings > General): the stars of the menus'
 * background turn slowly around the big moon at the bottom right. Kept
 * free of Vita calls: tested on a PC (tools/tests/test_library.c).
 *
 * The theme's picture is then drawn without its stars (bg_<theme>_sky.png) and
 * theme.c draws those of stars.h (orbit_star), placed here. They fill the whole
 * ring the screen sees while it turns. The horizon of the still picture is
 * applied where each star is at the time: towards the bottom of the screen
 * fewer stars show (each one fades out below its own height, its depth) and
 * they are dimmer, so that the sky looks at every moment like the still one.
 * Four speeds and both directions can be chosen (config.sky_speed,
 * config.sky_anticlockwise). */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define ORBIT_SCREEN_W  960.0f
#define ORBIT_SCREEN_H  544.0f
#define ORBIT_FADE      0.04f          /* how gradually a star sets (in (1 - y/H)^1.4) */
#define ORBIT_MIN_SPREAD 0.85f         /* px: smaller dots shimmer as they cross pixels */
#define ORBIT_MARGIN    6.0f           /* px: a dot this far outside the screen still shows a little */
#define ORBIT_MAX_STEP_US 250000u      /* the sky turns while it is seen: a longer gap (a stream,
                                          a hitch) moves it no further than this */
#define ORBIT_TWO_PI    6.283185307179586

/* Time for one turn at each speed of Settings > General > Moving sky
 * (config.sky_speed), from very slow to fast; 0: still. Each speed is twice
 * the one before. */
#define ORBIT_SPEEDS 5
static const uint32_t k_orbit_turn_s[ORBIT_SPEEDS] = {0, 32 * 60, 16 * 60, 8 * 60, 4 * 60};

/* The angle turned (radians, 0 to 2 pi) after `elapsed_us` more at `speed`.
 * The sky keeps its place when the speed or the direction changes: only the
 * angle moves on, from where it was. Clockwise on the screen: stars rise at
 * the bottom left, cross the top to the right and go down past the right
 * edge, above the moon; anticlockwise the other way. */
static inline double orbit_advance(double angle, uint64_t elapsed_us, int speed, bool anticlockwise) {
  if (speed <= 0 || speed >= ORBIT_SPEEDS)
    return angle;
  if (elapsed_us > ORBIT_MAX_STEP_US)
    elapsed_us = ORBIT_MAX_STEP_US;
  double step = ORBIT_TWO_PI * (double)elapsed_us / (k_orbit_turn_s[speed] * 1e6);
  angle += anticlockwise ? -step : step;
  angle = fmod(angle, ORBIT_TWO_PI);
  return angle < 0.0 ? angle + ORBIT_TWO_PI : angle;
}

/* Where a star is, given cos and sin of the angle turned (orbit_advance()). */
static inline void orbit_place(float cx, float cy, float dx, float dy, float c, float s,
                               float *x, float *y) {
  *x = cx + dx * c - dy * s;
  *y = cy + dx * s + dy * c;
}

static inline float orbit_clamp01(float v) {
  return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

static inline float orbit_smooth(float e0, float e1, float v) {
  float t = orbit_clamp01((v - e0) / (e1 - e0));
  return t * t * (3.0f - 2.0f * t);
}

/* (1 - y/H)^1.4, the share of stars star_list() keeps at height y: 1 at the
 * top, 0 at the bottom. A cubic instead of powf(): within 0.006 on [0, 1],
 * exact at both ends, rising (tools/tests/test_library.c). */
static inline float orbit_keep(float y) {
  float x = orbit_clamp01(1.0f - y / ORBIT_SCREEN_H);
  return x * (0.3660f + x * (0.9127f - 0.2787f * x));
}

/* 0 (hidden) to 1: whether a star of this depth shows at height y. */
static inline float orbit_horizon(float y, float depth) {
  return orbit_smooth(depth - ORBIT_FADE, depth + ORBIT_FADE, orbit_keep(y));
}

/* 0 to 1: the top bar's text (name on the left, status on the right) stays
 * clear, as in the still picture (no star above y 58 there); smooth, so that
 * a star going in or out fades instead of popping. */
static inline float orbit_top_bar(float x, float y) {
  float band = 1.0f - orbit_smooth(44.0f, 66.0f, y);
  float text = 1.0f - orbit_smooth(270.0f, 300.0f, x);
  float right = orbit_smooth(670.0f, 700.0f, x);
  if (right > text)
    text = right;
  return 1.0f - band * text;
}

/* How bright a star is where it is: its brightness in the picture
 * (make_background.py), dimmed by the horizon like star_list() (1 - 0.6 y/H),
 * faded below its depth and near the top bar's text; 0 off the screen. */
static inline float orbit_light(float x, float y, float bright, float depth) {
  if (x < -ORBIT_MARGIN || x > ORBIT_SCREEN_W + ORBIT_MARGIN || y < -ORBIT_MARGIN ||
      y > ORBIT_SCREEN_H + ORBIT_MARGIN)
    return 0.0f;
  float dim = 1.0f - 0.6f * orbit_clamp01(y / ORBIT_SCREEN_H);
  return bright * dim * orbit_horizon(y, depth) * orbit_top_bar(x, y);
}

/* How wide a star is drawn (its gaussian's sigma, px) and its opacity at the
 * centre: make_background.py adds `light` at the centre with a spread of
 * `size`; a star spread wider is made fainter so that it gives the same
 * light in all. */
static inline float orbit_spread(float size) {
  return size < ORBIT_MIN_SPREAD ? ORBIT_MIN_SPREAD : size;
}

static inline float orbit_alpha(float size, float light) {
  float spread = orbit_spread(size);
  return light / 255.0f * (size * size) / (spread * spread);
}

#endif // ORBIT_H
