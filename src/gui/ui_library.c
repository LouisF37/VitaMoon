/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Library screen; see ui_library.h.
 *
 * Layout (960x544): top bar, a "hero" block for the selected game (title,
 * last-session figures, actions), a shelf of covers at the bottom and the
 * hint row. Covers come from the box-art cache (boxart.c); until a cover is
 * downloaded a coloured tile with the game's initials stands in for it.
 */
#include "ui_library.h"
#include "boxart.h"
#include "guilib.h"
#include "theme.h"
#include "../config.h"
#include "../connection.h"
#include "../video/stream_stats.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int read_buttons();
void ui_start();
void ui_end();

#define MAX_ITEMS 128
#define COVER_W 120.0f
#define COVER_H 160.0f
#define COVER_FW 150.0f       /* focused cover */
#define COVER_FH 200.0f
#define COVER_GAP 16.0f
#define SHELF_X 48.0f
#define SHELF_BOTTOM 486.0f
#define HERO_X 48
#define HERO_MAX_W 600

typedef struct {
  int id;
  char name[96];
  char initials[4];
  unsigned int tint;
  vita2d_texture *tex;
  int retry_in;               /* frames before looking for the cover again */
  float focus;                /* 0..1, animated */
  bool stats_loaded, has_stats;
  stream_last_session last;
} lib_item;

static lib_item items[MAX_ITEMS];
static int item_count = 0;
static int last_focus_id = 0;   /* remembered between visits */

static const unsigned int tints[] = {
  RGBA8(0x3a, 0x6f, 0xb0, 0xFF), RGBA8(0x2f, 0x8f, 0x9a, 0xFF), RGBA8(0xb0, 0x6a, 0x3a, 0xFF),
  RGBA8(0x6b, 0x5a, 0xa8, 0xFF), RGBA8(0x4a, 0x8a, 0x5a, 0xFF), RGBA8(0x7a, 0x85, 0x97, 0xFF),
};

static void make_initials(const char *name, char out[4]) {
  int n = 0;
  bool start = true;
  for (const char *p = name; *p && n < 3; p++) {
    if (*p == ' ' || *p == '-' || *p == ':') {
      start = true;
    } else if (start && isalnum((unsigned char)*p)) {
      out[n++] = (char)toupper((unsigned char)*p);
      start = false;
    } else {
      start = false;
    }
  }
  if (n == 0)
    out[n++] = '?';
  out[n] = 0;
}

static void load_items(PAPP_LIST apps) {
  item_count = 0;
  for (PAPP_LIST a = apps; a && item_count < MAX_ITEMS; a = a->next) {
    lib_item *it = &items[item_count];
    memset(it, 0, sizeof(*it));
    it->id = a->id;
    snprintf(it->name, sizeof(it->name), "%s", a->name ? a->name : "?");
    make_initials(it->name, it->initials);
    it->tint = tints[(unsigned)a->id % (sizeof(tints) / sizeof(tints[0]))];
    item_count++;
  }
}

static void free_items(void) {
  for (int i = 0; i < item_count; i++) {
    if (items[i].tex) {
      vita2d_free_texture(items[i].tex);
      items[i].tex = NULL;
    }
  }
  item_count = 0;
}

/* Text truncated with "..." to fit max_w. */
static void draw_text_fit(vita2d_font *f, int x, int y, unsigned int color, unsigned int size,
                          const char *text, int max_w) {
  if (theme_text_width(f, size, text) <= max_w) {
    vita2d_font_draw_text(f, x, y, color, size, text);
    return;
  }
  char buf[128];
  int len = (int)strlen(text);
  if (len > (int)sizeof(buf) - 4) len = sizeof(buf) - 4;
  while (len > 0) {
    snprintf(buf, sizeof(buf), "%.*s...", len, text);
    if (theme_text_width(f, size, buf) <= max_w)
      break;
    len--;
    while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80) len--;  /* stay on a UTF-8 boundary */
  }
  vita2d_font_draw_text(f, x, y, color, size, buf);
}

/* A pill holding "<value><label>", value in bold colour. Returns the x after it. */
static int draw_chip(int x, int y, const char *value, const char *label, unsigned int value_color) {
  vita2d_font *f = theme_font();
  int vw = theme_text_width(f, 14, value), lw = theme_text_width(f, 14, label);
  int w = vw + lw + 20;
  theme_rounded_rect(x, y, w, 26, 8, RGBA8(6, 12, 28, 160));
  theme_rounded_outline(x, y, w, 26, 8, TH_LINE_FAINT);
  vita2d_font_draw_text(f, x + 10, y + 18, value_color, 14, value);
  vita2d_font_draw_text(f, x + 10 + vw, y + 18, TH_TEXT2, 14, label);
  return x + w + 8;
}

/* An action button: glyph + caption, primary ones filled with the accent. */
static int draw_action(int x, int y, int glyph, const char *caption, bool primary) {
  vita2d_font *f = theme_font();
  int tw = theme_text_width(f, 15, caption);
  int w = 10 + 20 + 8 + tw + 16;
  if (primary) {
    theme_rounded_rect(x - 2, y - 2, w + 4, 40, 12, RGBA8(109, 180, 255, 60));
    theme_rounded_rect(x, y, w, 36, 10, TH_ACCENT);
  } else {
    theme_rounded_rect(x, y, w, 36, 10, TH_FILL_FOCUS);
    theme_rounded_outline(x, y, w, 36, 10, TH_PANEL_EDGE);
  }
  theme_glyph(glyph, x + 20, y + 18, 10);
  vita2d_font_draw_text(f, x + 38, y + 24, primary ? TH_INK : TH_TEXT, 15, caption);
  return x + w + 10;
}

static void draw_cover(lib_item *it, float x, float bottom, float w, float h, bool focused, bool running) {
  float y = bottom - h;
  theme_rounded_rect(x + 2, y + 6, w, h, 10, RGBA8(0, 0, 0, 90));  /* shadow */
  if (it->tex) {
    vita2d_draw_texture_scale(it->tex, x, y, w / BOXART_W, h / BOXART_H);
  } else {
    theme_rounded_rect(x, y, w, h, 10, it->tint);
    theme_vgradient(x, y + h * 0.55f, w, h * 0.45f - 10, RGBA8(2, 5, 14, 0), RGBA8(2, 5, 14, 150));
    vita2d_font_draw_text(theme_font_light(), x + 10, y + 38, RGBA8(255, 255, 255, 225), focused ? 34 : 28, it->initials);
    draw_text_fit(theme_font(), (int)x + 8, (int)(y + h - 12), TH_TEXT, 12, it->name, (int)w - 14);
  }
  if (running) {
    theme_rounded_rect(x + 8, y + 8, 64, 20, 10, TH_OK);
    vita2d_font_draw_text(theme_font(), x + 15, y + 23, TH_INK, 12, "en cours");
  }
  if (focused) {
    float r = it->tex ? 4 : 11;
    theme_rounded_outline(x - 2, y - 2, w + 4, h + 4, r, TH_TEXT);
    theme_rounded_outline(x - 3, y - 3, w + 6, h + 6, r + 1, RGBA8(255, 255, 255, 140));
  }
}

static void draw_hero(lib_item *it, const char *host_name, bool running, bool other_running) {
  vita2d_font *f = theme_font();
  int y = 84;

  char kicker[96];
  snprintf(kicker, sizeof(kicker), "%s", host_name && *host_name ? host_name : "PC");
  for (char *p = kicker; *p; p++) *p = (char)toupper((unsigned char)*p);
  vita2d_font_draw_text(f, HERO_X, y, TH_ACCENT_HI, 13, kicker);

  draw_text_fit(theme_font_light(), HERO_X, y + 42, TH_TEXT, 36, it->name, HERO_MAX_W);

  if (!it->stats_loaded) {
    it->has_stats = stream_stats_read_last(it->id, &it->last);
    it->stats_loaded = true;
  }

  char meta[160];
  if (running)
    snprintf(meta, sizeof(meta), "En cours sur le PC : reprends là où tu en étais");
  else if (it->has_stats)
    snprintf(meta, sizeof(meta), "Dernière session : %s, %u min", it->last.when, (it->last.seconds + 59) / 60);
  else
    snprintf(meta, sizeof(meta), "Jamais lancé depuis cette Vita");
  vita2d_font_draw_text(f, HERO_X, y + 70, TH_TEXT3, 15, meta);

  int cy = y + 88;
  if (it->has_stats) {
    char v[32];
    int x = HERO_X;
    snprintf(v, sizeof(v), "%d,%d", it->last.shown_fps_x10 / 10, it->last.shown_fps_x10 % 10);
    x = draw_chip(x, cy, v, " img/s affichées", it->last.shown_fps_x10 >= 575 ? TH_OK : TH_TEXT);
    snprintf(v, sizeof(v), "%d ms", (it->last.r2p_ms_x10 + 5) / 10);
    x = draw_chip(x, cy, v, " réception > écran", TH_TEXT);
    snprintf(v, sizeof(v), "%u", it->last.lost);
    draw_chip(x, cy, v, it->last.lost > 1 ? " images perdues" : " image perdue", it->last.lost ? TH_WARN : TH_TEXT);
  }

  int ay = it->has_stats ? cy + 42 : cy + 4;
  int x = HERO_X;
  if (other_running) {
    x = draw_action(x, ay, GLYPH_SQUARE, "Quitter le jeu en cours", true);
  } else {
    x = draw_action(x, ay, theme_confirm_glyph(), running ? "Reprendre" : "Jouer", true);
    if (running)
      x = draw_action(x, ay, GLYPH_SQUARE, "Quitter le jeu", false);
  }
  draw_action(x, ay, GLYPH_TRIANGLE, "Options", false);
}

int ui_library(PSERVER_DATA server, PAPP_LIST apps, const char *host_name, int *app_id) {
  load_items(apps);
  if (item_count == 0)
    return LIBRARY_OPTIONS;

  int focus = 0;
  int wanted = server->currentGame ? server->currentGame : last_focus_id;
  for (int i = 0; i < item_count; i++) {
    if (items[i].id == wanted) {
      focus = i;
      break;
    }
  }
  items[focus].focus = 1.0f;

  float scroll = 0.0f;
  bool touching = false;
  int result = LIBRARY_BACK;

  while (true) {
    if (connection_get_status() == LI_DISCONNECTED) {
      result = LIBRARY_BACK;
      break;
    }
    int running_id = server->currentGame;

    /* ---- input ---- */
    int buttons = read_buttons();
    bool hold = (buttons & SCE_CTRL_HOLD) != 0;
    int step = 0;
    if (buttons & SCE_CTRL_RIGHT) step = 1;
    if (buttons & SCE_CTRL_LEFT) step = -1;
    if (buttons & SCE_CTRL_RTRIGGER) step = 5;
    if (buttons & SCE_CTRL_LTRIGGER) step = -5;
    if (step) {
      focus += step;
      if (focus < 0) focus = 0;
      if (focus >= item_count) focus = item_count - 1;
    }

    int tapped = -1;
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    if (touch.reportNum > 0 && !touching) {
      float tx = touch.report[0].x / 2.0f, ty = touch.report[0].y / 2.0f;
      float x = SHELF_X - scroll;
      for (int i = 0; i < item_count; i++) {
        float w = COVER_W + (COVER_FW - COVER_W) * items[i].focus;
        float h = COVER_H + (COVER_FH - COVER_H) * items[i].focus;
        if (tx >= x && tx <= x + w && ty >= SHELF_BOTTOM - h && ty <= SHELF_BOTTOM) {
          tapped = i;
          break;
        }
        x += w + COVER_GAP;
      }
    }
    touching = touch.reportNum > 0;

    lib_item *cur = &items[focus];
    bool running = running_id != 0 && cur->id == running_id;
    bool other_running = running_id != 0 && !running;

    if (tapped >= 0 && tapped != focus) {
      focus = tapped;
    } else if (((buttons & config.btn_confirm) && !hold) || tapped == focus) {
      if (other_running) {
        display_error("Un autre jeu tourne déjà sur le PC.\nQuitte-le d'abord avec la touche carré.");
      } else {
        *app_id = cur->id;
        result = LIBRARY_LAUNCH;
        break;
      }
    } else if ((buttons & SCE_CTRL_SQUARE) && !hold && running_id != 0) {
      result = LIBRARY_QUIT_APP;
      break;
    } else if ((buttons & SCE_CTRL_TRIANGLE) && !hold) {
      result = LIBRARY_OPTIONS;
      break;
    } else if ((buttons & SCE_CTRL_START) && !hold) {
      result = LIBRARY_SETTINGS;
      break;
    } else if ((buttons & config.btn_cancel) && !hold) {
      result = LIBRARY_BACK;
      break;
    }

    /* ---- animation: focus grows, shelf scrolls ---- */
    for (int i = 0; i < item_count; i++) {
      float target = i == focus ? 1.0f : 0.0f;
      items[i].focus += (target - items[i].focus) * 0.3f;
    }
    float slot = COVER_W + COVER_GAP;
    float scroll_target = focus * slot - 2 * slot;
    float max_scroll = item_count * slot + (COVER_FW - COVER_W) - (WIDTH - SHELF_X - 24);
    if (scroll_target > max_scroll) scroll_target = max_scroll;
    if (scroll_target < 0) scroll_target = 0;
    scroll += (scroll_target - scroll) * 0.25f;

    /* ---- covers: load at most one per frame, retry while downloads finish ---- */
    bool loaded_one = false;
    float probe_x = SHELF_X - scroll;
    for (int i = 0; i < item_count && !loaded_one; i++) {
      float w = COVER_W + (COVER_FW - COVER_W) * items[i].focus;
      bool visible = probe_x + w > -COVER_FW && probe_x < WIDTH + COVER_FW;
      probe_x += w + COVER_GAP;
      if (!visible || items[i].tex)
        continue;
      if (items[i].retry_in > 0) {
        items[i].retry_in--;
        continue;
      }
      items[i].tex = boxart_load_texture(server, items[i].id);
      if (!items[i].tex)
        items[i].retry_in = 90;
      loaded_one = true;
    }

    /* the focus may have changed above */
    cur = &items[focus];
    running = running_id != 0 && cur->id == running_id;
    other_running = running_id != 0 && !running;

    /* ---- draw ---- */
    ui_start();
    theme_topbar(host_name);
    draw_hero(cur, host_name, running, other_running);

    char counter[32];
    snprintf(counter, sizeof(counter), "%d / %d", focus + 1, item_count);
    int cw = theme_text_width(theme_font(), 13, counter);
    vita2d_font_draw_text(theme_font(), WIDTH - 48 - cw, SHELF_BOTTOM - COVER_FH - 14, TH_TEXT3, 13, counter);

    float x = SHELF_X - scroll;
    for (int i = 0; i < item_count; i++) {
      float w = COVER_W + (COVER_FW - COVER_W) * items[i].focus;
      float h = COVER_H + (COVER_FH - COVER_H) * items[i].focus;
      if (x + w > 0 && x < WIDTH && i != focus)
        draw_cover(&items[i], x, SHELF_BOTTOM, w, h, false, items[i].id == running_id);
      x += w + COVER_GAP;
    }
    /* focused cover last, so its outline is on top */
    x = SHELF_X - scroll;
    for (int i = 0; i < focus; i++)
      x += COVER_W + (COVER_FW - COVER_W) * items[i].focus + COVER_GAP;
    draw_cover(cur, x, SHELF_BOTTOM, COVER_W + (COVER_FW - COVER_W) * cur->focus,
               COVER_H + (COVER_FH - COVER_H) * cur->focus, true, running);

    int hx = 24;
    if (other_running) {
      hx = theme_hint(hx, TH_HINTS_Y, GLYPH_SQUARE, "Quitter le jeu en cours");
    } else {
      hx = theme_hint(hx, TH_HINTS_Y, theme_confirm_glyph(), running ? "Reprendre" : "Jouer");
      if (running)
        hx = theme_hint(hx, TH_HINTS_Y, GLYPH_SQUARE, "Quitter le jeu");
    }
    hx = theme_hint(hx, TH_HINTS_Y, GLYPH_TRIANGLE, "Options");
    hx = theme_hint(hx, TH_HINTS_Y, GLYPH_START, "Réglages");
    theme_hint(hx, TH_HINTS_Y, theme_cancel_glyph(), "Retour");
    ui_end();
  }

  last_focus_id = items[focus].id;
  free_items();
  return result;
}
