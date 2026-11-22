/*
 * This file is part of VitaMoon.
 *
 * The shape of the game covers: rounded corners, like the coloured tiles shown
 * until a cover is downloaded. Pure C, so the PC tests can check it.
 */
#pragma once

#include <stdint.h>

/* Corner radius in the 192 x 256 cover picture (BOXART_W x BOXART_H). Covers
 * are drawn scaled, so on screen the radius is a twelfth of the width: 10 px
 * for a 120 px cover, the same as the tiles. */
#define COVER_RADIUS_TEX 16.0f

/* Corner radius on screen for a cover or a tile drawn w pixels wide. */
static inline float cover_radius(float w) { return w / 12.0f; }

/* Makes the four corners of an RGBA picture transparent, with a smoothed edge.
 * Done once when a cover is loaded: drawing it then costs nothing more, and it
 * looks right over any background. */
void cover_round_corners(uint8_t *rgba, unsigned width, unsigned height, unsigned stride, float radius);
