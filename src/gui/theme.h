/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Visual theme shared by every screen: the "Ocean" palette of VitaRPS5, the
 * Roboto fonts, the wave background and a few drawing primitives (rounded
 * panels, gradients, PlayStation button glyphs, top bar, hint row).
 */
#pragma once

#include <stdbool.h>
#include <vita2d.h>

#define TH_TEXT        RGBA8(0xFF, 0xFF, 0xFF, 0xFF)
#define TH_TEXT2       RGBA8(0xDD, 0xE3, 0xF0, 0xFF)
#define TH_TEXT3       RGBA8(0xB3, 0xBD, 0xD2, 0xFF)
#define TH_ACCENT      RGBA8(109, 180, 255, 0xFF)
#define TH_ACCENT_HI   RGBA8(160, 205, 255, 0xFF)
#define TH_INK         RGBA8(2, 5, 14, 0xFF)
#define TH_OK          RGBA8(0x4E, 0xE0, 0x9A, 0xFF)
#define TH_WARN        RGBA8(0xFF, 0xBE, 0x4D, 0xFF)
#define TH_ERR         RGBA8(0xFF, 0x80, 0x80, 0xFF)
#define TH_PANEL       RGBA8(6, 12, 28, 190)       /* glass panels */
#define TH_PANEL_EDGE  RGBA8(255, 255, 255, 36)
#define TH_FILL_FOCUS  RGBA8(255, 255, 255, 31)
#define TH_FILL_SOFT   RGBA8(255, 255, 255, 20)
#define TH_LINE_FAINT  RGBA8(255, 255, 255, 36)
#define TH_SHADE       RGBA8(2, 5, 14, 150)
#define TH_GLYPH_X     RGBA8(0xB2, 0xDE, 0xFA, 0xFF)
#define TH_GLYPH_O     RGBA8(0xF6, 0xC5, 0xD0, 0xFF)
#define TH_GLYPH_TRI   RGBA8(0xC3, 0xF2, 0xD0, 0xFF)
#define TH_GLYPH_SQ    RGBA8(0xFF, 0xDC, 0x7B, 0xFF)

#define TH_TOPBAR_H 32
#define TH_HINTS_Y  530   /* baseline of the hint row */

enum { GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_START, GLYPH_LR };

void theme_init(void);

vita2d_font *theme_font(void);        /* Roboto Regular (falls back to the icon font) */
vita2d_font *theme_font_light(void);  /* Roboto Light */
int theme_text_width(vita2d_font *f, unsigned int size, const char *text);
/* Copies text into out, cut with an ellipsis if it is wider than max_w.
   Cuts on UTF-8 boundaries so accented letters are never split. */
void theme_ellipsize(vita2d_font *f, unsigned int size, const char *text, int max_w,
                     char *out, int outlen);

void theme_draw_background(void);
void theme_rounded_rect(float x, float y, float w, float h, float r, unsigned int color);
void theme_rounded_outline(float x, float y, float w, float h, float r, unsigned int color);
void theme_glass(float x, float y, float w, float h, float r);
void theme_vgradient(float x, float y, float w, float h, unsigned int top, unsigned int bottom);

/* PlayStation button glyph in a coloured disc centred on (cx, cy). */
void theme_glyph(int glyph, float cx, float cy, float r);
/* Glyph + label on the hint row; returns the x after it. */
int theme_hint(int x, int baseline, int glyph, const char *label);
/* Confirm / cancel glyphs, following the "Swap X & O" setting. */
int theme_confirm_glyph(void);
int theme_cancel_glyph(void);

/* "Moonlight" + optional section on the left, clock and battery on the right. */
void theme_topbar(const char *section);
