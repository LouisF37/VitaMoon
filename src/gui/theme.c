/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Visual theme; see theme.h.
 */
#include "theme.h"
#include "guilib.h"
#include "../config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/kernel/processmgr.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CORNER_SEGMENTS 6

static vita2d_font *font_regular = NULL;
static vita2d_font *font_light = NULL;
static vita2d_texture *background = NULL;

void theme_init(void) {
  font_regular = vita2d_load_font_file("app0:assets/Roboto-Regular.ttf");
  font_light = vita2d_load_font_file("app0:assets/Roboto-Light.ttf");
  background = vita2d_load_PNG_file("app0:assets/bg_ocean.png");
}

vita2d_font *theme_font(void) {
  return font_regular ? font_regular : font;
}

vita2d_font *theme_font_light(void) {
  return font_light ? font_light : theme_font();
}

int theme_text_width(vita2d_font *f, unsigned int size, const char *text) {
  return text && *text ? vita2d_font_text_width(f, size, text) : 0;
}

void theme_ellipsize(vita2d_font *f, unsigned int size, const char *text, int max_w,
                     char *out, int outlen) {
  static const char ellipsis[] = "\xE2\x80\xA6"; /* U+2026 */
  snprintf(out, outlen, "%s", text ? text : "");
  if (max_w <= 0 || theme_text_width(f, size, out) <= max_w)
    return;
  int len = strlen(out);
  while (len > 0) {
    /* step back one UTF-8 character */
    do { len--; } while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80);
    while (len > 0 && out[len - 1] == ' ')
      len--;
    if (len + (int)sizeof(ellipsis) > outlen)
      continue;
    memcpy(out + len, ellipsis, sizeof(ellipsis));
    if (theme_text_width(f, size, out) <= max_w)
      return;
  }
  snprintf(out, outlen, "%s", ellipsis);
}

void theme_draw_background(void) {
  if (background)
    vita2d_draw_texture(background, 0, 0);
  else
    theme_vgradient(0, 0, WIDTH, HEIGHT, RGBA8(6, 32, 74, 0xFF), RGBA8(36, 95, 156, 0xFF));
}

/* Rounded rectangle as one triangle fan: no overlapping pieces, so translucent
 * colours stay uniform. */
void theme_rounded_rect(float x, float y, float w, float h, float r, unsigned int color) {
  if (w <= 0 || h <= 0)
    return;
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

void theme_glass(float x, float y, float w, float h, float r) {
  theme_rounded_rect(x, y + 4, w, h, r, RGBA8(0, 0, 0, 60));   /* soft shadow */
  theme_rounded_rect(x, y, w, h, r, TH_PANEL);
  theme_rounded_outline(x, y, w, h, r, TH_PANEL_EDGE);
}

void theme_vgradient(float x, float y, float w, float h, unsigned int top, unsigned int bottom) {
  vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v)
    return;
  v[0] = (vita2d_color_vertex){x, y, 0.5f, top};
  v[1] = (vita2d_color_vertex){x + w, y, 0.5f, top};
  v[2] = (vita2d_color_vertex){x, y + h, 0.5f, bottom};
  v[3] = (vita2d_color_vertex){x + w, y + h, 0.5f, bottom};
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
}

/* A line drawn 2 px thick (vita2d lines are 1 px). */
static void thick_line(float x0, float y0, float x1, float y1, unsigned int color) {
  vita2d_draw_line(x0, y0, x1, y1, color);
  vita2d_draw_line(x0 + 0.7f, y0, x1 + 0.7f, y1, color);
  vita2d_draw_line(x0, y0 + 0.7f, x1, y1 + 0.7f, color);
}

void theme_glyph(int glyph, float cx, float cy, float r) {
  unsigned int disc;
  switch (glyph) {
    case GLYPH_CROSS: disc = TH_GLYPH_X; break;
    case GLYPH_CIRCLE: disc = TH_GLYPH_O; break;
    case GLYPH_TRIANGLE: disc = TH_GLYPH_TRI; break;
    case GLYPH_SQUARE: disc = TH_GLYPH_SQ; break;
    default: disc = RGBA8(255, 255, 255, 220); break;
  }
  if (glyph == GLYPH_START || glyph == GLYPH_LR) {
    const char *label = glyph == GLYPH_START ? "START" : "L R";
    int tw = theme_text_width(theme_font(), 11, label);
    theme_rounded_rect(cx - r, cy - r * 0.8f, tw + 10, r * 1.6f, 5, disc);
    vita2d_font_draw_text(theme_font(), cx - r + 5, cy + 4, TH_INK, 11, label);
    return;
  }
  vita2d_draw_fill_circle(cx, cy, r, disc);
  float s = r * 0.45f;
  switch (glyph) {
    case GLYPH_CROSS:
      thick_line(cx - s, cy - s, cx + s, cy + s, TH_INK);
      thick_line(cx - s, cy + s, cx + s, cy - s, TH_INK);
      break;
    case GLYPH_CIRCLE:
      vita2d_draw_fill_circle(cx, cy, s + 1.5f, TH_INK);
      vita2d_draw_fill_circle(cx, cy, s - 0.5f, disc);
      break;
    case GLYPH_TRIANGLE:
      thick_line(cx, cy - s, cx + s, cy + s * 0.75f, TH_INK);
      thick_line(cx + s, cy + s * 0.75f, cx - s, cy + s * 0.75f, TH_INK);
      thick_line(cx - s, cy + s * 0.75f, cx, cy - s, TH_INK);
      break;
    case GLYPH_SQUARE:
      thick_line(cx - s, cy - s, cx + s, cy - s, TH_INK);
      thick_line(cx + s, cy - s, cx + s, cy + s, TH_INK);
      thick_line(cx + s, cy + s, cx - s, cy + s, TH_INK);
      thick_line(cx - s, cy + s, cx - s, cy - s, TH_INK);
      break;
  }
}

int theme_hint(int x, int baseline, int glyph, const char *label) {
  float r = 10;
  int glyph_w;
  if (glyph == GLYPH_START || glyph == GLYPH_LR) {
    glyph_w = theme_text_width(theme_font(), 11, glyph == GLYPH_START ? "START" : "L R") + 10;
    theme_glyph(glyph, x + r, baseline - 5, r);
  } else {
    glyph_w = (int)(r * 2);
    theme_glyph(glyph, x + r, baseline - 5, r);
  }
  int tx = x + glyph_w + 7;
  vita2d_font_draw_text(theme_font(), tx, baseline, TH_TEXT2, 15, label);
  return tx + theme_text_width(theme_font(), 15, label) + 22;
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
  static SceUInt64 last_poll = 0;
  SceUInt64 now = sceKernelGetProcessTimeWide();
  if (battery < 0 || now - last_poll > 10000000) {
    battery = scePowerGetBatteryLifePercent();
    charging = scePowerIsBatteryCharging();
    last_poll = now;
  }

  theme_vgradient(0, 0, WIDTH, 44, RGBA8(2, 5, 14, 110), RGBA8(2, 5, 14, 0));
  vita2d_font_draw_text(theme_font(), 24, 22, TH_TEXT, 16, "Moonlight");
  if (section && *section) {
    int x = 24 + theme_text_width(theme_font(), 16, "Moonlight") + 14;
    int w = theme_text_width(theme_font(), 14, section);
    theme_rounded_rect(x - 10, 6, w + 20, 22, 11, TH_FILL_FOCUS);
    vita2d_font_draw_text(theme_font(), x, 22, TH_TEXT, 14, section);
  }

  SceDateTime t;
  sceRtcGetCurrentClockLocalTime(&t);
  char clock[16];
  snprintf(clock, sizeof(clock), "%02d:%02d", t.hour, t.minute);
  int clock_w = theme_text_width(theme_font(), 15, clock);
  int x = WIDTH - 24 - clock_w;
  vita2d_font_draw_text(theme_font(), x, 22, TH_TEXT2, 15, clock);

  /* battery: outline, fill, cap */
  int bx = x - 16 - 26, by = 9;
  unsigned int fill = charging ? TH_OK : (battery < 20 ? TH_ERR : TH_TEXT2);
  theme_rounded_outline(bx, by, 24, 13, 3, TH_TEXT2);
  vita2d_draw_rectangle(bx + 24, by + 4, 2, 5, TH_TEXT2);
  int fw = battery > 0 ? (20 * battery) / 100 : 0;
  if (fw > 0)
    theme_rounded_rect(bx + 2, by + 2, fw, 9, 2, fill);
  char pct[16];
  snprintf(pct, sizeof(pct), "%d%%", battery < 0 ? 0 : battery);
  int pw = theme_text_width(theme_font(), 13, pct);
  vita2d_font_draw_text(theme_font(), bx - 8 - pw, 21, TH_TEXT3, 13, pct);
}
