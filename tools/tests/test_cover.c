/*
 * Rounded covers: src/gui/cover_shape.c makes the corners of a loaded cover
 * transparent once, so that it is drawn like the coloured tiles.
 */
#include "test.h"
#include "cover_shape.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W 192
#define H 256
#define STRIDE (W * 4 + 32)   /* a row longer than the picture, as a texture's can be */

static uint8_t *picture(void) {
  uint8_t *p = malloc((size_t)STRIDE * H);
  memset(p, 0x5A, (size_t)STRIDE * H);   /* the padding too: it must stay untouched */
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t *q = p + (size_t)y * STRIDE + x * 4;
      q[0] = (uint8_t)x;
      q[1] = (uint8_t)y;
      q[2] = 200;
      q[3] = 255;
    }
  return p;
}

static uint8_t alpha(const uint8_t *p, int x, int y) { return p[(size_t)y * STRIDE + x * 4 + 3]; }

int main(void) {
  TEST_BEGIN("cover_shape");
  uint8_t *p = picture();
  uint8_t *ref = picture();
  cover_round_corners(p, W, H, STRIDE, COVER_RADIUS_TEX);

  CHECK(alpha(p, 0, 0) == 0 && alpha(p, W - 1, 0) == 0 && alpha(p, 0, H - 1) == 0 && alpha(p, W - 1, H - 1) == 0,
        "the four corner pixels are transparent");
  bool symmetric = true;
  for (int y = 0; y < 20; y++)
    for (int x = 0; x < 20; x++) {
      uint8_t a = alpha(p, x, y);
      if (alpha(p, W - 1 - x, y) != a || alpha(p, x, H - 1 - y) != a || alpha(p, W - 1 - x, H - 1 - y) != a)
        symmetric = false;
    }
  CHECK(symmetric, "the four corners are the same");

  /* outside the corner squares nothing changes, colours and padding included */
  bool same_outside = true, same_colour = true;
  int r = (int)ceilf(COVER_RADIUS_TEX);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < STRIDE / 4; x++) {
      size_t o = (size_t)y * STRIDE + x * 4;
      bool in_corner = x < W && (x < r || x >= W - r) && (y < r || y >= H - r);
      if (!in_corner && memcmp(p + o, ref + o, 4) != 0)
        same_outside = false;
      if (memcmp(p + o, ref + o, 3) != 0)
        same_colour = false;
    }
  CHECK(same_outside, "pixels outside the corners (and the row padding) are untouched");
  CHECK(same_colour, "colours are kept: no dark fringe once filtered");

  /* along the diagonal, alpha rises from 0 to 255 over about one pixel: a smooth edge */
  int partial = 0;
  for (int i = 0; i < r; i++) {
    uint8_t a = alpha(p, i, i);
    if (a > 0 && a < 255)
      partial++;
  }
  CHECK(partial >= 1 && partial <= 2, "the edge is smoothed over one or two pixels");
  bool rising = true;
  for (int i = 1; i < r; i++)
    if (alpha(p, i, i) < alpha(p, i - 1, i - 1))
      rising = false;
  CHECK(rising, "opacity only grows from the corner inwards");
  CHECK(alpha(p, r, 0) == 255 && alpha(p, 0, r) == 255, "the straight edges start right after the radius");

  /* the transparent area is the corner outside a quarter circle: about r^2 (1 - pi/4) per corner */
  double cleared = 0;
  for (int y = 0; y < r; y++)
    for (int x = 0; x < r; x++)
      cleared += (255 - alpha(p, x, y)) / 255.0;
  double expected = COVER_RADIUS_TEX * COVER_RADIUS_TEX * (1 - M_PI / 4);
  CHECK(fabs(cleared - expected) < 1.5, "the cleared area matches a circle of the radius");

  /* already transparent pixels stay so; a radius larger than the picture is clamped */
  uint8_t small[8 * 6 * 4];
  memset(small, 255, sizeof(small));
  cover_round_corners(small, 8, 6, 8 * 4, 100);
  CHECK(small[3] == 0 && small[(5 * 8 + 7) * 4 + 3] == 0 && small[(3 * 8 + 4) * 4 + 3] == 255,
        "a radius too large for the picture is reduced to half its height");
  cover_round_corners(NULL, W, H, STRIDE, COVER_RADIUS_TEX);
  CHECK(1, "no picture: nothing happens");

  /* on screen, the radius is a twelfth of the width: 10 px for a 120 px cover */
  CHECK(fabsf(cover_radius(120) - 10) < 0.001f, "a 120 px cover has 10 px corners, like the tiles");
  CHECK(fabsf(cover_radius(192) - COVER_RADIUS_TEX) < 0.001f, "a cover drawn at its own size keeps the picture's radius");

  free(p);
  free(ref);
  return TEST_END();
}
