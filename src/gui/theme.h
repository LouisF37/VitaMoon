/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Visual theme shared by every screen: a lunar sky behind the menus, Inter for
 * the text and Poppins (the logo's typeface) for the titles, thin dividers,
 * rounded focus highlight, outlined PlayStation symbols on the hint row. The
 * idea of colour themes comes from VitaRPS5. Five colour themes (Settings >
 * General > Theme) change the background and the colours below that follow
 * the theme; the others are the same in every theme.
 *
 * Everything is drawn on one grid (see the TH_* layout constants) so that
 * screens line up with each other, and all text goes through theme_text*():
 * each size has its own font instance, so glyphs are always drawn at the size
 * they were rasterised for (vita2d otherwise rescales the first size used).
 */
#pragma once

#include <stdbool.h>
#include <vita2d.h>

/* ---- colour themes ---- */
enum { THEME_NIGHT, THEME_DAWN, THEME_AURORA, THEME_TWILIGHT, THEME_ECLIPSE, THEME_COUNT };

/* The colours that follow the theme (vita2d RGBA8). */
typedef struct {
  unsigned int text2, text3;        /* secondary and tertiary text, tinted */
  unsigned int accent, accent_hi;   /* focus, toggles, highlights */
  unsigned int ink;                 /* darkest tone: shades and dims */
  unsigned int panel;               /* floating panels (with their opacity) */
  unsigned int sky_top, sky_bottom; /* background when its picture is missing */
} theme_palette;

extern theme_palette th_pal;

/* Applies a theme (THEME_*): its colours and its background picture. */
void theme_select(int index);
/* English name of a theme, to translate with tr(). */
const char *theme_name(int index);
/* The same colour with another opacity (0-255). */
unsigned int theme_alpha(unsigned int color, unsigned int alpha);

/* ---- colours (vita2d RGBA8) ---- */
#define TH_TEXT        RGBA8(0xFF, 0xFF, 0xFF, 0xFF)
#define TH_TEXT2       (th_pal.text2)
#define TH_TEXT3       (th_pal.text3)
#define TH_TEXT_OFF    theme_alpha(th_pal.text3, 0x8C)   /* unavailable rows */
#define TH_ACCENT      (th_pal.accent)
#define TH_ACCENT_HI   (th_pal.accent_hi)
#define TH_INK         (th_pal.ink)
#define TH_OK          RGBA8(0x4E, 0xE0, 0x9A, 0xFF)
#define TH_WARN        RGBA8(0xFF, 0xBE, 0x4D, 0xFF)
#define TH_ERR         RGBA8(0xFF, 0x80, 0x80, 0xFF)
#define TH_PANEL       (th_pal.panel)                    /* floating panels */
#define TH_PANEL_EDGE  RGBA8(255, 255, 255, 36)
#define TH_FILL_FOCUS  RGBA8(255, 255, 255, 30)
#define TH_FILL_SOFT   RGBA8(255, 255, 255, 18)
#define TH_DIVIDER     RGBA8(255, 255, 255, 38)
#define TH_LINE_FAINT  TH_DIVIDER
#define TH_SHADE       theme_alpha(th_pal.ink, 150)
#define TH_GLYPH_X     RGBA8(0x9F, 0xD0, 0xFF, 0xFF)
#define TH_GLYPH_O     RGBA8(0xFF, 0xA9, 0xC0, 0xFF)
#define TH_GLYPH_TRI   RGBA8(0x8F, 0xE8, 0xB4, 0xFF)
#define TH_GLYPH_SQ    RGBA8(0xF2, 0xB8, 0xE8, 0xFF)

/* ---- layout grid (the screen is 960x544) ---- */
#define TH_MARGIN        48    /* left edge of all content */
#define TH_RIGHT         912   /* right edge of all content */
#define TH_TOPBAR_BASE   40    /* baseline of the top bar */
#define TH_HEADER_BASE   100   /* baseline of a page title */
#define TH_HEADER_LINE   120   /* divider under the page title */
#define TH_CONTENT_Y     128   /* first row of a page with a title */
#define TH_CONTENT_Y0    72    /* first row of a page without a title */
#define TH_CONTENT_END   476   /* content never goes below this line */
#define TH_HINTS_Y       524   /* baseline of the hint row */

/* ---- type scale: use only these sizes ---- */
#define TS_MICRO   12
#define TS_SMALL   14
#define TS_HINT    16
#define TS_BODY    18
#define TS_CLOCK   20
#define TS_LEAD    22
#define TS_TITLE   28
#define TS_HERO    34

enum {
  GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_SQUARE,
  GLYPH_START, GLYPH_SELECT, GLYPH_LR, GLYPH_DPAD_LR
};

typedef struct {
  int glyph;
  const char *label;
} theme_hint_item;

void theme_init(void);

/* Global opacity for everything drawn through theme_*(), for fades. */
void theme_set_alpha(float alpha);
unsigned int theme_color(unsigned int color);

/* Moves current towards target, a fraction of the way per frame. */
float theme_approach(float current, float target);

/* ---- text (y is the baseline) ---- */
void theme_text(float x, float y, unsigned int color, int size, const char *text);
void theme_text_right(float right, float y, unsigned int color, int size, const char *text);
void theme_text_center(float cx, float y, unsigned int color, int size, const char *text);
/* Left-aligned, cut with an ellipsis if wider than max_w. */
void theme_text_fit(float x, float y, unsigned int color, int size, const char *text, int max_w);
/* Right-aligned at `right`, cut with an ellipsis if wider than max_w. */
void theme_text_fit_right(float right, float y, unsigned int color, int size, const char *text, int max_w);
/* Centred on cx, cut with an ellipsis if wider than max_w. */
void theme_text_fit_center(float cx, float y, unsigned int color, int size, const char *text, int max_w);
int theme_text_width(int size, const char *text);
/* Copies text into out, cut with an ellipsis if it is wider than max_w.
   Cuts on UTF-8 boundaries so accented letters are never split. */
void theme_ellipsize(int size, const char *text, int max_w, char *out, int outlen);
/* Splits text into lines (on spaces and newlines) that fit max_w; returns the count. */
int theme_wrap(int size, const char *text, int max_w, char lines[][256], int max_lines);

/* ---- shapes ---- */
void theme_draw_background(void);
void theme_rect(float x, float y, float w, float h, unsigned int color);
void theme_rounded_rect(float x, float y, float w, float h, float r, unsigned int color);
void theme_rounded_outline(float x, float y, float w, float h, float r, unsigned int color);
void theme_circle(float cx, float cy, float r, unsigned int color);
void theme_ring(float cx, float cy, float r, float thickness, unsigned int color);
void theme_stroke(float x0, float y0, float x1, float y1, float thickness, unsigned int color);
void theme_vgradient(float x, float y, float w, float h, unsigned int top, unsigned int bottom);
/* Floating panel: shadow, dark translucent fill, thin edge. */
void theme_glass(float x, float y, float w, float h, float r);

/* ---- widgets ---- */
/* "<" (dir = -1) or ">" (dir = +1) centred on (cx, cy). */
void theme_chevron(float cx, float cy, float size, int dir, unsigned int color);
/* On/off switch, 44x24, left edge at x, centred on cy. knob: 0 = off ... 1 = on. */
void theme_toggle(float x, float cy, float knob);
/* Focus highlight of a row: soft rounded fill with an accent bar on the left. */
void theme_focus(float x, float y, float w, float h);
/* Wi-Fi style signal bars (0-4 lit), bottom-left corner at (x, y). */
void theme_signal(float x, float y, int bars, unsigned int color);

/* PlayStation symbol centred on (cx, cy); returns its width. */
int theme_glyph(int glyph, float cx, float cy);
int theme_glyph_width(int glyph);
/* Hint row centred at the bottom of the screen. */
void theme_hints(const theme_hint_item *items, int count);
/* Same, centred on cx with the baseline at y (dialogs). */
void theme_hints_at(const theme_hint_item *items, int count, float cx, float y);
/* Confirm / cancel glyphs, following the "Swap X & O" setting. */
int theme_confirm_glyph(void);
int theme_cancel_glyph(void);

/* Top bar: the app name + optional section on the left; Wi-Fi, battery and
 * clock on the right. */
void theme_topbar(const char *section);
/* Page title with a back chevron and the divider under it. */
void theme_header(const char *title);
