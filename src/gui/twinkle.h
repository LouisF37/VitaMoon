#ifndef TWINKLE_H
#define TWINKLE_H

/* Twinkling stars ("Twinkling stars" in Settings > General): how bright each
 * star of the background is made at a given time. Kept free of Vita calls:
 * tested on a PC (tools/tests/test_library.c). The stars themselves are those of
 * the theme's picture (stars.h); theme.c draws a soft white dot over each.
 *
 * Every star rests most of the time and brightens gently once in its own
 * period (2.5 to 6 s), at its own moment, so that only a few are brightening
 * at any time; the change is smooth (no flicker from one frame to the next). */

#include <stdint.h>

#define TWINKLE_PEAK       0.45f    /* white over the brightest star, at most */
#define TWINKLE_BRIGHTEST  115.0f   /* brightest star of make_background.py */
#define TWINKLE_ACTIVE     0.45f    /* part of its period a star brightens */
#define TWINKLE_PERIOD_MIN 2500000u /* us */
#define TWINKLE_PERIOD_SPAN 3500000u

static inline uint32_t twinkle_hash(uint32_t i) {
  uint32_t h = (i + 1u) * 2654435761u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return h;
}

/* 0 (resting) to 1 (brightest), for star `index` at time t_us. */
static inline float twinkle_level(uint32_t index, uint64_t t_us) {
  uint32_t h = twinkle_hash(index);
  uint32_t period = TWINKLE_PERIOD_MIN + h % TWINKLE_PERIOD_SPAN;
  uint32_t phase = (h >> 7) % period;
  float u = (float)((t_us + phase) % period) / (float)period;
  if (u >= TWINKLE_ACTIVE)
    return 0.0f;
  /* a smooth bump from 0 to 1 and back over the active part: 4x(1-x), then
   * smoothstep so that it starts and ends softly */
  float x = u / TWINKLE_ACTIVE;
  float b = 4.0f * x * (1.0f - x);        /* 0 -> 1 -> 0 */
  return b * b * (3.0f - 2.0f * b);       /* smoothstep: soft at both ends */
}

/* Opacity of the white dot over a star of brightness `bright` (as drawn). */
static inline float twinkle_alpha(uint32_t index, float bright, uint64_t t_us) {
  float scale = bright / TWINKLE_BRIGHTEST;
  if (scale > 1.0f)
    scale = 1.0f;
  if (scale < 0.0f)
    scale = 0.0f;
  return TWINKLE_PEAK * scale * twinkle_level(index, t_us);
}

#endif // TWINKLE_H
