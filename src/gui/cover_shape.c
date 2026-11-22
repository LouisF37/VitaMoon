/*
 * This file is part of VitaMoon.
 */
#include "cover_shape.h"

#include <math.h>
#include <stddef.h>

void cover_round_corners(uint8_t *rgba, unsigned width, unsigned height, unsigned stride, float radius) {
  if (!rgba || radius <= 0)
    return;
  if (radius * 2 > width) radius = width / 2.0f;
  if (radius * 2 > height) radius = height / 2.0f;
  unsigned n = (unsigned)ceilf(radius);
  for (unsigned y = 0; y < n; y++) {
    for (unsigned x = 0; x < n; x++) {
      /* distance from the pixel's centre to the centre of the corner's circle */
      float dx = radius - (x + 0.5f), dy = radius - (y + 0.5f);
      if (dx <= 0 || dy <= 0)
        continue;
      float coverage = radius - sqrtf(dx * dx + dy * dy) + 0.5f;
      if (coverage >= 1)
        continue;
      if (coverage < 0)
        coverage = 0;
      /* the same pixel in each of the four corners */
      unsigned xs[2] = {x, width - 1 - x}, ys[2] = {y, height - 1 - y};
      for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
          uint8_t *p = rgba + (size_t)ys[j] * stride + (size_t)xs[i] * 4;
          p[3] = (uint8_t)(p[3] * coverage + 0.5f);
        }
      }
    }
  }
}
