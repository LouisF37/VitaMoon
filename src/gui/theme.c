/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Visual theme: see theme.h.
 */
#include "theme.h"
#include "guilib.h"
#include "text_cover.h"
#include "title_font_cover.h"
#include "../config.h"
#include "../app_info.h"
#include "../i18n.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/net/netctl.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef ASSET_PREFIX
#define ASSET_PREFIX "app0:assets/"
#endif

#define CORNER_SEGMENTS 8
#define RING_SEGMENTS 28

/* ---------------- fonts ---------------- */

/* One font instance per size: vita2d caches each glyph at the first size it
 * is drawn with and rescales it afterwards, which blurs every other size.
 * Inter for the text (Regular for the small sizes, Light from the body size),
 * Poppins, the logo's typeface, for the titles. Poppins has Latin only: a title
 * with a character it lacks (Greek, Cyrillic, some accented letters) is drawn
 * with Inter Light at the same size, an instance made the first time one is
 * needed. tools/art/make_fonts.py builds the three files and the table. */
enum { FONT_TEXT, FONT_TEXT_LIGHT, FONT_TITLE, FONT_FILES };
static const char *const k_font_files[FONT_FILES] = {
  ASSET_PREFIX "Inter-Regular.otf", ASSET_PREFIX "Inter-Light.otf", ASSET_PREFIX "Poppins-Regular.ttf",
};
static void *font_data[FONT_FILES];
static unsigned int font_size[FONT_FILES];

typedef struct {
  int size;
  int file;
  vita2d_font *font;
  vita2d_font *fallback;   /* titles only: Inter Light at this size */
} th_face;

static th_face faces[] = {
  {TS_MICRO, FONT_TEXT, NULL, NULL},       {TS_SMALL, FONT_TEXT, NULL, NULL},
  {TS_HINT, FONT_TEXT, NULL, NULL},        {TS_BODY, FONT_TEXT_LIGHT, NULL, NULL},
  {TS_CLOCK, FONT_TEXT_LIGHT, NULL, NULL}, {TS_LEAD, FONT_TITLE, NULL, NULL},
  {TS_TITLE, FONT_TITLE, NULL, NULL},      {TS_HERO, FONT_TITLE, NULL, NULL},
};
#define FACE_COUNT ((int)(sizeof(faces) / sizeof(faces[0])))

static vita2d_texture *background = NULL;
static float g_alpha = 1.0f;

/* ---------------- colour themes ---------------- */

/* Each theme: its name, its background picture (assets/bg_<slug>.png, drawn by
 * tools/make_background.py from the same sky colours) and its colours. The idea
 * of a few colour presets comes from VitaRPS5; these palettes are VitaMoon's own,
 * named after the sky. */
typedef struct {
  const char *name;
  const char *slug;
  theme_palette pal;
} theme_def;

_Static_assert(THEME_COUNT == CONFIG_THEME_COUNT, "config.c clamps the theme with CONFIG_THEME_COUNT");

#define C(r, g, b) RGBA8(r, g, b, 0xFF)
static const theme_def k_themes[THEME_COUNT] = {
  [THEME_NIGHT] = {N_("Night"), "night", {
    C(0xDD, 0xE3, 0xF0), C(0xB3, 0xBD, 0xD2), C(109, 180, 255), C(160, 205, 255),
    C(2, 5, 14), RGBA8(6, 12, 28, 200), C(6, 32, 74), C(36, 95, 156)}},
  [THEME_DAWN] = {N_("Dawn"), "dawn", {
    C(0xF2, 0xE2, 0xDA), C(0xD6, 0xBE, 0xB2), C(255, 152, 112), C(255, 192, 164),
    C(14, 4, 2), RGBA8(30, 11, 6, 200), C(40, 12, 8), C(132, 56, 34)}},
  [THEME_AURORA] = {N_("Aurora"), "aurora", {
    C(0xDA, 0xF0, 0xEA), C(0xB0, 0xD2, 0xC8), C(86, 226, 186), C(156, 242, 216),
    C(1, 11, 11), RGBA8(4, 24, 23, 200), C(2, 24, 26), C(9, 80, 76)}},
  [THEME_TWILIGHT] = {N_("Twilight"), "twilight", {
    C(0xE8, 0xDE, 0xF4), C(0xC6, 0xB6, 0xDC), C(196, 140, 255), C(222, 190, 255),
    C(8, 3, 16), RGBA8(20, 8, 36, 200), C(22, 8, 42), C(78, 36, 132)}},
  [THEME_ECLIPSE] = {N_("Eclipse"), "eclipse", {
    C(0xE6, 0xE4, 0xDE), C(0xC0, 0xBC, 0xB2), C(240, 192, 104), C(250, 218, 158),
    C(4, 4, 5), RGBA8(14, 14, 16, 200), C(10, 10, 12), C(36, 36, 42)}},
};
#undef C

theme_palette th_pal;
static int current_theme = -1;

unsigned int theme_alpha(unsigned int color, unsigned int alpha) {
  return (color & 0x00FFFFFFu) | ((alpha & 0xFFu) << 24);
}

const char *theme_name(int index) {
  return index >= 0 && index < THEME_COUNT ? k_themes[index].name : "";
}

/* VitaMoon's mark (tools/art/make_brand.py), left of the name in the top bar. */
static vita2d_texture *logo_mark;

void theme_select(int index) {
  if (index < 0 || index >= THEME_COUNT)
    index = THEME_NIGHT;
  th_pal = k_themes[index].pal;
  if (index == current_theme && background)
    return;
  current_theme = index;
  char path[96];
  snprintf(path, sizeof(path), ASSET_PREFIX "bg_%s.png", k_themes[index].slug);
  vita2d_texture *picture = vita2d_load_PNG_file(path);
  if (!picture)
    return;   /* keep the previous picture, or the plain gradient */
  if (background) {
    vita2d_wait_rendering_done();   /* the GPU may still read the old one */
    vita2d_free_texture(background);
  }
  background = picture;
}

/* Characters drawn once at start-up so that no glyph has to be rasterised in
 * the middle of an animation (ASCII plus what French and Spanish need). */
static const char *k_prewarm =
  " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
  "abcdefghijklmnopqrstuvwxyz{|}~"
  "\xC3\xA0\xC3\xA2\xC3\xA4\xC3\xA7\xC3\xA9\xC3\xA8\xC3\xAA\xC3\xAB\xC3\xAE\xC3\xAF"
  "\xC3\xB4\xC3\xB6\xC3\xB9\xC3\xBB\xC3\xBC\xC3\xA1\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xB1"
  "\xC3\x80\xC3\x89\xC3\x88\xC3\x8A\xC3\x87\xC3\x81\xC3\x8D\xC3\x93\xC3\x9A\xC3\x91"
  "\xC2\xBF\xC2\xA1\xC2\xAB\xC2\xBB\xC2\xB0\xC2\xB7\xC3\x97\xE2\x80\xA6\xE2\x80\x99";

static void *read_file(const char *path, unsigned int *size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *buf = n > 0 ? malloc(n) : NULL;
  if (buf && fread(buf, 1, n, f) != (size_t)n) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  *size = buf ? (unsigned int)n : 0;
  return buf;
}

void theme_init(void) {
  /* The font files are read once and kept: every size shares the same data. */
  for (int i = 0; i < FONT_FILES; i++)
    font_data[i] = read_file(k_font_files[i], &font_size[i]);
  for (int i = 0; i < FACE_COUNT; i++) {
    int file = font_data[faces[i].file] ? faces[i].file : FONT_TEXT;
    if (font_data[file])
      faces[i].font = vita2d_load_font_mem(font_data[file], font_size[file]);
    if (faces[i].font)
      vita2d_font_text_width(faces[i].font, faces[i].size, k_prewarm);
  }
  theme_select(config.theme);
  logo_mark = vita2d_load_PNG_file(ASSET_PREFIX "logo_mark.png");
}

static th_face *face_for(int size) {
  th_face *best = &faces[0];
  for (int i = 1; i < FACE_COUNT; i++) {
    if (abs(faces[i].size - size) < abs(best->size - size))
      best = &faces[i];
  }
  return best;
}

static vita2d_font *face_font(th_face *f, const char *text) {
  if (f->file == FONT_TITLE && f->font && !text_covered(text, k_title_cover, TITLE_COVER_COUNT)) {
    if (!f->fallback && font_data[FONT_TEXT_LIGHT])
      f->fallback = vita2d_load_font_mem(font_data[FONT_TEXT_LIGHT], font_size[FONT_TEXT_LIGHT]);
    if (f->fallback)
      return f->fallback;
  }
  return f->font ? f->font : font; /* icon font as a last resort */
}

/* ---------------- opacity and animation ---------------- */

void theme_set_alpha(float alpha) {
  g_alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}

unsigned int theme_color(unsigned int color) {
  if (g_alpha >= 0.999f)
    return color;
  unsigned int a = (unsigned int)(((color >> 24) & 0xFF) * g_alpha + 0.5f);
  return (color & 0x00FFFFFF) | (a << 24);
}

float theme_approach(float current, float target) {
  float d = target - current;
  if (d > -0.5f && d < 0.5f)
    return target;
  return current + d * 0.34f;
}

static unsigned int mix(unsigned int a, unsigned int b, float t) {
  unsigned int out = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    float ca = (float)((a >> shift) & 0xFF), cb = (float)((b >> shift) & 0xFF);
    out |= ((unsigned int)(ca + (cb - ca) * t + 0.5f) & 0xFF) << shift;
  }
  return out;
}

/* ---------------- text ---------------- */

int theme_text_width(int size, const char *text) {
  if (!text || !*text)
    return 0;
  th_face *f = face_for(size);
  return vita2d_font_text_width(face_font(f, text), f->size, text);
}

void theme_prepare_text(int size, const char *text) {
  (void)theme_text_width(size, text);   /* vita2d rasterises what it measures */
}

void theme_text(float x, float y, unsigned int color, int size, const char *text) {
  if (!text || !*text)
    return;
  th_face *f = face_for(size);
  vita2d_font_draw_text(face_font(f, text), (int)(x + 0.5f), (int)(y + 0.5f), theme_color(color), f->size, text);
}

void theme_text_right(float right, float y, unsigned int color, int size, const char *text) {
  theme_text(right - theme_text_width(size, text), y, color, size, text);
}

void theme_text_center(float cx, float y, unsigned int color, int size, const char *text) {
  theme_text(cx - theme_text_width(size, text) / 2.0f, y, color, size, text);
}

void theme_ellipsize(int size, const char *text, int max_w, char *out, int outlen) {
  static const char ellipsis[] = "\xE2\x80\xA6"; /* U+2026 */
  snprintf(out, outlen, "%s", text ? text : "");
  if (max_w <= 0 || theme_text_width(size, out) <= max_w)
    return;
#ifdef UI_PREVIEW
  fprintf(stderr, "TRUNCATED (%d px > %d): %s\n", theme_text_width(size, out), max_w, out);
#endif
  int len = strlen(out);
  while (len > 0) {
    /* step back one UTF-8 character */
    do { len--; } while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80);
    while (len > 0 && out[len - 1] == ' ')
      len--;
    if (len + (int)sizeof(ellipsis) > outlen)
      continue;
    memcpy(out + len, ellipsis, sizeof(ellipsis));
    if (theme_text_width(size, out) <= max_w)
      return;
  }
  snprintf(out, outlen, "%s", ellipsis);
}

void theme_text_fit(float x, float y, unsigned int color, int size, const char *text, int max_w) {
  char buf[256];
  theme_ellipsize(size, text, max_w, buf, sizeof(buf));
  theme_text(x, y, color, size, buf);
}

void theme_text_fit_right(float right, float y, unsigned int color, int size, const char *text, int max_w) {
  char buf[256];
  theme_ellipsize(size, text, max_w, buf, sizeof(buf));
  theme_text_right(right, y, color, size, buf);
}

void theme_text_fit_center(float cx, float y, unsigned int color, int size, const char *text, int max_w) {
  char buf[256];
  theme_ellipsize(size, text, max_w, buf, sizeof(buf));
  theme_text_center(cx, y, color, size, buf);
}

int theme_wrap(int size, const char *text, int max_w, char lines[][256], int max_lines) {
  int count = 0;
  char current[256] = "";
  const char *p = text ? text : "";
  while (*p && count < max_lines) {
    char word[200];
    int n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\n' && n < (int)sizeof(word) - 1) n++;
    memcpy(word, p, n);
    word[n] = 0;
    p += n;

    char candidate[512];
    snprintf(candidate, sizeof(candidate), "%s%s%s", current, *current ? " " : "", word);
    if (*current && theme_text_width(size, candidate) > max_w) {
      snprintf(lines[count++], 256, "%s", current);
      snprintf(current, sizeof(current), "%s", word);
    } else {
      snprintf(current, sizeof(current), "%.255s", candidate);
    }
    if (*p == '\n' || *p == 0) {
      if (count < max_lines && (*current || *p == '\n'))
        snprintf(lines[count++], 256, "%s", current);
      current[0] = 0;
    }
    if (*p) p++;
  }
  return count;
}

/* ---------------- shapes ---------------- */

void theme_draw_background(void) {
  if (background)
    vita2d_draw_texture(background, 0, 0);
  else
    theme_vgradient(0, 0, WIDTH, HEIGHT, th_pal.sky_top, th_pal.sky_bottom);
}

void theme_rect(float x, float y, float w, float h, unsigned int color) {
  if (w > 0 && h > 0)
    vita2d_draw_rectangle(x, y, w, h, theme_color(color));
}

/* Rounded rectangle as one triangle fan: no overlapping pieces, so translucent
 * colours stay uniform. */
void theme_rounded_rect(float x, float y, float w, float h, float r, unsigned int color) {
  if (w <= 0 || h <= 0)
    return;
  color = theme_color(color);
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r < 1.0f) {
    vita2d_draw_rectangle(x, y, w, h, color);
    return;
  }
  const int count = 1 + 4 * (CORNER_SEGMENTS + 1) + 1;
  vita2d_color_vertex *v = vita2d_pool_memalign(count * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v)
    return;
  const float cx[4] = {x + w - r, x + w - r, x + r, x + r};
  const float cy[4] = {y + r, y + h - r, y + h - r, y + r};
  const float a0[4] = {-M_PI / 2, 0, M_PI / 2, M_PI};
  int k = 0;
  v[k++] = (vita2d_color_vertex){x + w / 2, y + h / 2, 0.5f, color};
  for (int c = 0; c < 4; c++) {
    for (int i = 0; i <= CORNER_SEGMENTS; i++) {
      float a = a0[c] + (float)(M_PI / 2) * i / CORNER_SEGMENTS;
      v[k++] = (vita2d_color_vertex){cx[c] + r * cosf(a), cy[c] + r * sinf(a), 0.5f, color};
    }
  }
  v[k++] = v[1];
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_FAN, v, k);
}

void theme_rounded_outline(float x, float y, float w, float h, float r, unsigned int color) {
  if (w <= 0 || h <= 0)
    return;
  color = theme_color(color);
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  const int points = 4 * (CORNER_SEGMENTS + 1);
  vita2d_color_vertex *v = vita2d_pool_memalign(points * 2 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v)
    return;
  const float cx[4] = {x + w - r, x + w - r, x + r, x + r};
  const float cy[4] = {y + r, y + h - r, y + h - r, y + r};
  const float a0[4] = {-M_PI / 2, 0, M_PI / 2, M_PI};
  float px[4 * (CORNER_SEGMENTS + 1)], py[4 * (CORNER_SEGMENTS + 1)];
  int n = 0;
  for (int c = 0; c < 4; c++) {
    for (int i = 0; i <= CORNER_SEGMENTS; i++) {
      float a = a0[c] + (float)(M_PI / 2) * i / CORNER_SEGMENTS;
      px[n] = cx[c] + r * cosf(a);
      py[n] = cy[c] + r * sinf(a);
      n++;
    }
  }
  int k = 0;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    v[k++] = (vita2d_color_vertex){px[i], py[i], 0.5f, color};
    v[k++] = (vita2d_color_vertex){px[j], py[j], 0.5f, color};
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_LINES, v, k);
}

void theme_circle(float cx, float cy, float r, unsigned int color) {
  vita2d_draw_fill_circle(cx, cy, r, theme_color(color));
}

void theme_ring(float cx, float cy, float r, float thickness, unsigned int color) {
  color = theme_color(color);
  const int count = 2 * (RING_SEGMENTS + 1);
  vita2d_color_vertex *v = vita2d_pool_memalign(count * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v)
    return;
  float r0 = r - thickness / 2, r1 = r + thickness / 2;
  for (int i = 0; i <= RING_SEGMENTS; i++) {
    float a = (float)(2 * M_PI) * i / RING_SEGMENTS;
    float c = cosf(a), s = sinf(a);
    v[2 * i] = (vita2d_color_vertex){cx + r1 * c, cy + r1 * s, 0.5f, color};
    v[2 * i + 1] = (vita2d_color_vertex){cx + r0 * c, cy + r0 * s, 0.5f, color};
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, count);
}

/* A line of a given thickness, drawn as a quad. */
void theme_stroke(float x0, float y0, float x1, float y1, float thickness, unsigned int color) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f)
    return;
  color = theme_color(color);
  float nx = -dy / len * thickness / 2, ny = dx / len * thickness / 2;
  vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v)
    return;
  v[0] = (vita2d_color_vertex){x0 + nx, y0 + ny, 0.5f, color};
  v[1] = (vita2d_color_vertex){x0 - nx, y0 - ny, 0.5f, color};
  v[2] = (vita2d_color_vertex){x1 + nx, y1 + ny, 0.5f, color};
  v[3] = (vita2d_color_vertex){x1 - nx, y1 - ny, 0.5f, color};
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
}

void theme_vgradient(float x, float y, float w, float h, unsigned int top, unsigned int bottom) {
  vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v)
    return;
  top = theme_color(top);
  bottom = theme_color(bottom);
  v[0] = (vita2d_color_vertex){x, y, 0.5f, top};
  v[1] = (vita2d_color_vertex){x + w, y, 0.5f, top};
  v[2] = (vita2d_color_vertex){x, y + h, 0.5f, bottom};
  v[3] = (vita2d_color_vertex){x + w, y + h, 0.5f, bottom};
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
}

void theme_glass(float x, float y, float w, float h, float r) {
  theme_rounded_rect(x, y + 4, w, h, r, RGBA8(0, 0, 0, 70));   /* soft shadow */
  theme_rounded_rect(x, y, w, h, r, TH_PANEL);
  theme_rounded_outline(x, y, w, h, r, TH_PANEL_EDGE);
}

/* ---------------- widgets ---------------- */

void theme_chevron(float cx, float cy, float size, int dir, unsigned int color) {
  /* two strokes at 45 degrees meeting at the tip */
  float tip_x = cx + (dir < 0 ? -size : size) * 0.5f;
  float back_x = cx - (dir < 0 ? -size : size) * 0.5f;
  theme_stroke(back_x, cy - size, tip_x, cy, 2.0f, color);
  theme_stroke(tip_x, cy, back_x, cy + size, 2.0f, color);
  theme_circle(tip_x, cy, 1.0f, color);   /* closes the joint */
}

void theme_toggle(float x, float cy, float knob) {
  if (knob < 0.0f) knob = 0.0f;
  if (knob > 1.0f) knob = 1.0f;
  const float w = 44, h = 24;
  unsigned int track = mix(RGBA8(255, 255, 255, 40), theme_alpha(TH_ACCENT, 235), knob);
  theme_rounded_rect(x, cy - h / 2, w, h, h / 2, track);
  theme_rounded_outline(x, cy - h / 2, w, h, h / 2, mix(RGBA8(255, 255, 255, 120), TH_ACCENT_HI, knob));
  theme_circle(x + h / 2 + knob * (w - h), cy, h / 2 - 3.5f, mix(RGBA8(215, 222, 235, 255), TH_TEXT, knob));
}

void theme_focus(float x, float y, float w, float h) {
  theme_rounded_rect(x, y, w, h, 8, TH_FILL_FOCUS);
  theme_rounded_rect(x, y + h * 0.22f, 3, h * 0.56f, 1.5f, TH_TEXT);
}

void theme_signal(float x, float y, int bars, unsigned int color) {
  for (int i = 0; i < 4; i++) {
    float h = 4 + i * 3;
    unsigned int c = i < bars ? color : ((color & 0x00FFFFFF) | (60u << 24));
    theme_rounded_rect(x + i * 5, y - h, 3, h, 1, c);
  }
}

static const char *glyph_label(int glyph) {
  switch (glyph) {
    case GLYPH_START: return "START";
    case GLYPH_SELECT: return "SELECT";
    default: return "";
  }
}

int theme_glyph_width(int glyph) {
  switch (glyph) {
    case GLYPH_START:
    case GLYPH_SELECT:
      return theme_text_width(TS_MICRO, glyph_label(glyph)) + 16;
    case GLYPH_LR: return 52;
    case GLYPH_DPAD_LR: return 26;
    default: return 18;
  }
}

static void pill_label(float x, float cy, float w, const char *label) {
  theme_rounded_outline(x, cy - 9, w, 18, 6, TH_TEXT2);
  theme_text_center(x + w / 2, cy + 4, TH_TEXT2, TS_MICRO, label);
}

int theme_glyph(int glyph, float cx, float cy) {
  const float s = 6.5f;
  int w = theme_glyph_width(glyph);
  switch (glyph) {
    case GLYPH_CROSS:
      theme_stroke(cx - s, cy - s, cx + s, cy + s, 2.0f, TH_GLYPH_X);
      theme_stroke(cx - s, cy + s, cx + s, cy - s, 2.0f, TH_GLYPH_X);
      break;
    case GLYPH_CIRCLE:
      theme_ring(cx, cy, 7.5f, 2.0f, TH_GLYPH_O);
      break;
    case GLYPH_TRIANGLE: {
      float top = cy - 7.5f, bottom = cy + 5.5f, half = 7.5f;
      theme_stroke(cx, top, cx + half, bottom, 2.0f, TH_GLYPH_TRI);
      theme_stroke(cx + half + 0.7f, bottom, cx - half - 0.7f, bottom, 2.0f, TH_GLYPH_TRI);
      theme_stroke(cx - half, bottom, cx, top, 2.0f, TH_GLYPH_TRI);
      break;
    }
    case GLYPH_SQUARE:
      theme_stroke(cx - s - 1, cy - s, cx + s + 1, cy - s, 2.0f, TH_GLYPH_SQ);
      theme_stroke(cx + s, cy - s, cx + s, cy + s, 2.0f, TH_GLYPH_SQ);
      theme_stroke(cx + s + 1, cy + s, cx - s - 1, cy + s, 2.0f, TH_GLYPH_SQ);
      theme_stroke(cx - s, cy + s, cx - s, cy - s, 2.0f, TH_GLYPH_SQ);
      break;
    case GLYPH_START:
    case GLYPH_SELECT:
      pill_label(cx - w / 2.0f, cy, w, glyph_label(glyph));
      break;
    case GLYPH_LR:
      pill_label(cx - 26, cy, 24, "L");
      pill_label(cx + 2, cy, 24, "R");
      break;
    case GLYPH_DPAD_LR:
      theme_chevron(cx - 7, cy, 5.5f, -1, TH_TEXT2);
      theme_chevron(cx + 7, cy, 5.5f, 1, TH_TEXT2);
      break;
  }
  return w;
}

void theme_hints_at(const theme_hint_item *items, int count, float cx, float y) {
  const int gap = 28, pad = 9;
  int total = 0;
  for (int i = 0; i < count; i++)
    total += theme_glyph_width(items[i].glyph) + pad + theme_text_width(TS_HINT, items[i].label) + (i ? gap : 0);
  float x = cx - total / 2.0f;
  for (int i = 0; i < count; i++) {
    int gw = theme_glyph_width(items[i].glyph);
    theme_glyph(items[i].glyph, x + gw / 2.0f, y - 6);
    x += gw + pad;
    theme_text(x, y, TH_TEXT2, TS_HINT, items[i].label);
    x += theme_text_width(TS_HINT, items[i].label) + gap;
  }
}

void theme_hints(const theme_hint_item *items, int count) {
  theme_hints_at(items, count, WIDTH / 2.0f, TH_HINTS_Y);
}

int theme_confirm_glyph(void) {
  return config.jp_layout ? GLYPH_CIRCLE : GLYPH_CROSS;
}

int theme_cancel_glyph(void) {
  return config.jp_layout ? GLYPH_CROSS : GLYPH_CIRCLE;
}

void theme_topbar(const char *section) {
  static int battery = -1;
  static bool charging = false;
  static int signal = -1;
  static SceUInt64 last_poll = 0;
  SceUInt64 now = sceKernelGetProcessTimeWide();
  if (battery < 0 || now - last_poll > 5000000) {
    battery = scePowerGetBatteryLifePercent();
    charging = scePowerIsBatteryCharging();
    SceNetCtlInfo info;
    signal = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &info) >= 0
                 ? (int)info.rssi_percentage : -1;
    last_poll = now;
  }
  if (battery < 0) battery = 0;
  if (battery > 100) battery = 100;

  theme_vgradient(0, 0, WIDTH, 64, theme_alpha(TH_INK, 120), theme_alpha(TH_INK, 0));

  /* left: mark, name and section */
  int name_x = TH_MARGIN;
  if (logo_mark) {
    const float size = 28.0f;   /* the disc is 68 % of the picture: about the name's height */
    vita2d_draw_texture_scale(logo_mark, TH_MARGIN - 3, TH_TOPBAR_BASE - 23,
                              size / vita2d_texture_get_width(logo_mark),
                              size / vita2d_texture_get_height(logo_mark));
    name_x += 30;
  }
  theme_text(name_x, TH_TOPBAR_BASE, TH_TEXT, TS_LEAD, APP_NAME);
  if (section && *section) {
    int x = name_x + theme_text_width(TS_LEAD, APP_NAME) + 16;
    int w = theme_text_width(TS_SMALL, section);
    theme_rounded_rect(x, 21, w + 24, 26, 13, TH_FILL_FOCUS);
    theme_text(x + 12, 39, TH_TEXT, TS_SMALL, section);
  }

  /* right: clock, battery, signal (laid out from the right edge) */
  SceDateTime t;
  sceRtcGetCurrentClockLocalTime(&t);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
  theme_text_right(TH_RIGHT, TH_TOPBAR_BASE, TH_TEXT, TS_CLOCK, buf);
  /* the clock keeps a fixed width so the icons do not move when digits change */
  int x = TH_RIGHT - theme_text_width(TS_CLOCK, "00:00") - 18;

  snprintf(buf, sizeof(buf), "%d%%", battery);
  theme_text_right(x, 39, TH_TEXT2, TS_SMALL, buf);
  x -= theme_text_width(TS_SMALL, "100%") + 8;

  unsigned int fill = charging ? TH_OK : (battery < 20 ? TH_ERR : TH_TEXT);
  int bx = x - 26, by = 27;
  theme_rounded_outline(bx, by, 23, 12, 3, TH_TEXT);
  theme_rect(bx + 23.5f, by + 3.5f, 2, 5, TH_TEXT);
  int fw = (19 * battery + 50) / 100;
  if (fw > 0)
    theme_rounded_rect(bx + 2, by + 2, fw, 8, 1.5f, fill);
  x = bx - 18;

  int bars = signal < 0 ? 0 : (signal >= 75 ? 4 : signal >= 50 ? 3 : signal >= 25 ? 2 : 1);
  theme_signal(x - 18, 39, bars, signal < 0 ? TH_ERR : TH_TEXT);
}

void theme_header(const char *title) {
  theme_chevron(30, TH_HEADER_BASE - 10, 8, -1, TH_TEXT2);
  theme_text(TH_MARGIN, TH_HEADER_BASE, TH_TEXT, TS_TITLE, title);
  theme_rect(TH_MARGIN, TH_HEADER_LINE, TH_RIGHT - TH_MARGIN, 1, TH_DIVIDER);
}
