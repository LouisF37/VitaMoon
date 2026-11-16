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
 * when it was last played, actions; the last session's figures with the
 * setting "Statistics in the library"), a shelf of covers at the bottom and
 * the hint row. Covers come from the box-art cache (boxart.c); until a cover is
 * downloaded a coloured tile with the game's initials stands in for it.
 *
 * Order of the shelf (library_logic.h): favourites first (SELECT, kept in
 * <data folder>/favorites.txt), then the most recently played, then the games
 * never played from this Vita in the PC's order.
 */
#include "ui_library.h"
#include "preview_hooks.h"
#include "boxart.h"
#include "guilib.h"
#include "theme.h"
#include "../config.h"
#include "../connection.h"
#include "../video/stream_stats.h"
#include "../log.h"
#include "library_logic.h"

#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "../i18n.h"

#define MAX_ITEMS 128
#define COVER_W 120.0f
#define COVER_H 160.0f
#define COVER_FW 150.0f       /* focused cover */
#define COVER_FH 200.0f
#define COVER_GAP 16.0f
#define SHELF_X ((float)TH_MARGIN)
#define SHELF_BOTTOM 478.0f
#define HERO_X TH_MARGIN
#define HERO_MAX_W 640
#define HERO_TITLE_Y 112      /* baseline of the game title */
#define HERO_META_Y 140
#define HERO_CHIPS_Y 156
#define ACTION_H 38
#define LIB_STAR_COLOR RGBA8(0xF5, 0xC5, 0x42, 0xFF)   /* favourite star, every theme */

typedef struct {
  int id;
  char name[96];
  char initials[4];
  unsigned int tint;
  vita2d_texture *tex;
  int retry_in;               /* frames before looking for the cover again */
  float focus;                /* 0..1, animated */
  bool has_stats;
  stream_last_session last;
  bool favourite;
  int pc_order;               /* position in the PC's list (Sunshine's order) */
} lib_item;

static lib_item items[MAX_ITEMS];
static int item_count = 0;
static int last_focus_id = 0;   /* remembered between visits */

static const unsigned int tints[] = {
  RGBA8(0x3a, 0x6f, 0xb0, 0xFF), RGBA8(0x2f, 0x8f, 0x9a, 0xFF), RGBA8(0xb0, 0x6a, 0x3a, 0xFF),
  RGBA8(0x6b, 0x5a, 0xa8, 0xFF), RGBA8(0x4a, 0x8a, 0x5a, 0xFF), RGBA8(0xa4, 0x55, 0x74, 0xFF),
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

/* ---------------- favourites ---------------- */

/* App ids, one per line in <data folder>/favorites.txt. Like the last-session
 * files (stats/last_<id>.txt) they are kept by app id, whatever the PC. */
#define MAX_FAVOURITES 256
static int favourites[MAX_FAVOURITES];
static int favourite_count = 0;

static void favourites_path(char *out, size_t n) {
  snprintf(out, n, "%.400s/favorites.txt", config.key_dir);
}

static void favourites_load(void) {
  char path[512];
  favourites_path(path, sizeof(path));
  favourite_count = 0;
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  int id;
  while (favourite_count < MAX_FAVOURITES && fscanf(f, "%d", &id) == 1)
    favourites[favourite_count++] = id;
  fclose(f);
}

static void favourites_save(void) {
  char path[512];
  favourites_path(path, sizeof(path));
  FILE *f = fopen(path, "w");
  if (!f) {
    LOGW("library", "favourites not saved: %s cannot be written", path);
    return;
  }
  for (int i = 0; i < favourite_count; i++)
    fprintf(f, "%d\n", favourites[i]);
  fclose(f);
}

static bool is_favourite(int id) {
  for (int i = 0; i < favourite_count; i++)
    if (favourites[i] == id)
      return true;
  return false;
}

static void set_favourite(int id, bool on) {
  for (int i = 0; i < favourite_count; i++) {
    if (favourites[i] == id) {
      if (!on) {
        favourites[i] = favourites[--favourite_count];
        favourites_save();
      }
      return;
    }
  }
  if (on && favourite_count < MAX_FAVOURITES) {
    favourites[favourite_count++] = id;
    favourites_save();
  }
}

/* ---------------- the shelf ---------------- */

static lib_sort_key sort_key(const lib_item *it) {
  lib_sort_key k = {it->favourite, it->has_stats ? it->last.when : "", it->pc_order};
  return k;
}

/* Stable insertion sort: a few dozen games at most, already almost in order. */
static void sort_items(void) {
  for (int i = 1; i < item_count; i++) {
    lib_item moving = items[i];
    lib_sort_key key = sort_key(&moving);
    int j = i - 1;
    while (j >= 0) {
      lib_sort_key other = sort_key(&items[j]);
      if (lib_compare(&other, &key) <= 0)
        break;
      items[j + 1] = items[j];
      j--;
    }
    items[j + 1] = moving;
  }
}

static int index_of(int id) {
  for (int i = 0; i < item_count; i++)
    if (items[i].id == id)
      return i;
  return 0;
}

static void load_items(PAPP_LIST apps) {
  favourites_load();
  /* libgamestream links the apps in reverse: count them to number them as the PC lists them */
  int total = 0;
  for (PAPP_LIST a = apps; a && total < MAX_ITEMS; a = a->next)
    total++;
  item_count = 0;
  for (PAPP_LIST a = apps; a && item_count < MAX_ITEMS; a = a->next) {
    lib_item *it = &items[item_count];
    memset(it, 0, sizeof(*it));
    it->id = a->id;
    snprintf(it->name, sizeof(it->name), "%s", a->name ? a->name : "?");
    make_initials(it->name, it->initials);
    it->tint = tints[(unsigned)a->id % (sizeof(tints) / sizeof(tints[0]))];
    it->has_stats = stream_stats_read_last(it->id, &it->last);
    it->favourite = is_favourite(it->id);
    it->pc_order = total - 1 - item_count;
    item_count++;
  }
  sort_items();
}

/* "today at 19:46 · 6 min", "3 days ago · 1 h 12"... for the hero's line. */
static void describe_last_session(const lib_item *it, char *out, size_t n) {
  char when[64], length[32];
  lib_moment then, today;
  SceDateTime now;
  if (lib_parse_moment(it->last.when, &then) && sceRtcGetCurrentClockLocalTime(&now) >= 0) {
    today = (lib_moment){now.year, now.month, now.day, now.hour, now.minute};
    int days = 0;
    switch (lib_when(&then, &today, &days)) {
      case LIB_WHEN_TODAY:
        snprintf(when, sizeof(when), tr("today at %02d:%02d"), then.hour, then.minute);
        break;
      case LIB_WHEN_YESTERDAY:
        snprintf(when, sizeof(when), tr("yesterday at %02d:%02d"), then.hour, then.minute);
        break;
      case LIB_WHEN_DAYS_AGO:
        snprintf(when, sizeof(when), tr("%d days ago"), days);
        break;
      default:
        snprintf(when, sizeof(when), tr("on %02d/%02d/%04d"), then.day, then.month, then.year);
        break;
    }
  } else {
    snprintf(when, sizeof(when), "%s", it->last.when);
  }
  unsigned int hours, minutes;
  lib_duration_parts(it->last.seconds, &hours, &minutes);
  if (hours)
    snprintf(length, sizeof(length), tr("%u h %02u"), hours, minutes);
  else
    snprintf(length, sizeof(length), tr("%u min"), minutes);
  snprintf(out, n, tr("Last played %s · %s"), when, length);
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

/* A pill holding "<value><label>", the value in a stronger colour. Returns the x after it. */
static int draw_chip(int x, int y, const char *value, const char *label, unsigned int value_color) {
  int vw = theme_text_width(TS_SMALL, value), lw = theme_text_width(TS_SMALL, label);
  int w = vw + lw + 24;
  theme_rounded_rect(x, y, w, 26, 13, theme_alpha(TH_PANEL, 150));
  theme_rounded_outline(x, y, w, 26, 13, TH_DIVIDER);
  theme_text(x + 12, y + 18, value_color, TS_SMALL, value);
  theme_text(x + 12 + vw, y + 18, TH_TEXT2, TS_SMALL, label);
  return x + w + 8;
}

/* Action button; the primary one is filled with the accent colour.
 * Returns its width (also the touch target). */
static int draw_action(int x, int y, const char *caption, bool primary) {
  int w = theme_text_width(TS_HINT, caption) + 44;
  if (w < 120) w = 120;
  if (primary) {
    theme_rounded_rect(x, y, w, ACTION_H, ACTION_H / 2, TH_ACCENT);
  } else {
    theme_rounded_rect(x, y, w, ACTION_H, ACTION_H / 2, TH_FILL_SOFT);
    theme_rounded_outline(x, y, w, ACTION_H, ACTION_H / 2, RGBA8(255, 255, 255, 110));
  }
  theme_text_center(x + w / 2.0f, y + ACTION_H / 2.0f + TS_HINT * 0.36f, primary ? TH_INK : TH_TEXT, TS_HINT, caption);
  return w;
}

static void draw_cover(lib_item *it, float x, float bottom, float w, float h, bool focused, bool running) {
  float y = bottom - h;
  theme_rounded_rect(x + 2, y + 6, w, h, 10, RGBA8(0, 0, 0, 90));  /* shadow */
  if (it->tex) {
    vita2d_draw_texture_scale(it->tex, x, y, w / BOXART_W, h / BOXART_H);
  } else {
    /* no cover yet: a coloured tile with the initials and the name */
    theme_rounded_rect(x, y, w, h, 10, it->tint);
    /* the initials move down when the "running" badge takes the top-left corner */
    theme_text(x + 12, y + (running ? 66 : 42), RGBA8(255, 255, 255, 225), TS_TITLE, it->initials);
    theme_rounded_rect(x + 6, y + h - 30, w - 12, 24, 7, theme_alpha(TH_INK, 120));
    theme_text_fit(x + 13, y + h - 13, TH_TEXT, TS_MICRO, it->name, (int)w - 26);
  }
  if (running) {
    const char *label = tr("running");
    int bw = theme_text_width(TS_MICRO, label) + 16;
    theme_rounded_rect(x + 8, y + 8, bw, 20, 10, TH_OK);
    theme_text(x + 16, y + 22, TH_INK, TS_MICRO, label);
  }
  if (it->favourite && font) {
    /* a star in a dark disc, top right */
    float cx = x + w - 17, cy = y + 17;
    theme_circle(cx, cy, 12, theme_alpha(TH_INK, 170));
    int sw = vita2d_font_text_width(font, ICON_STAR_SIZE, ICON_STAR);
    vita2d_font_draw_text(font, (int)(cx - sw / 2.0f + 0.5f), (int)(cy + ICON_STAR_SIZE * 0.36f + 0.5f),
                          theme_color(LIB_STAR_COLOR), ICON_STAR_SIZE, ICON_STAR);
  }
  if (focused) {
    float r = it->tex ? 4 : 11;
    theme_rounded_outline(x - 2, y - 2, w + 4, h + 4, r, TH_TEXT);
    theme_rounded_outline(x - 3, y - 3, w + 6, h + 6, r + 1, RGBA8(255, 255, 255, 140));
  }
}

/* Touch targets of the hero buttons, refreshed on every draw. */
static struct { int x, y, w; } play_button, quit_button;

static void draw_hero(lib_item *it, bool running, bool other_running) {
  theme_text_fit(HERO_X, HERO_TITLE_Y, TH_TEXT, TS_HERO, it->name, HERO_MAX_W);

  char meta[200];
  if (running)
    snprintf(meta, sizeof(meta), "%s", tr("Running on the PC: pick up where you left off"));
  else if (it->has_stats)
    describe_last_session(it, meta, sizeof(meta));
  else
    snprintf(meta, sizeof(meta), "%s", tr("Never played from this Vita"));
  theme_text_fit(HERO_X, HERO_META_Y, TH_TEXT3, TS_HINT, meta, HERO_MAX_W);

  int ay = HERO_CHIPS_Y + 4;
  if (it->has_stats && config.library_stats) {
    char v[32];
    int x = HERO_X;
    snprintf(v, sizeof(v), tr("%d.%d"), it->last.shown_fps_x10 / 10, it->last.shown_fps_x10 % 10);
    x = draw_chip(x, HERO_CHIPS_Y, v, tr(" fps shown"), it->last.shown_fps_x10 >= 575 ? TH_OK : TH_TEXT);
    snprintf(v, sizeof(v), "%d ms", (it->last.r2p_ms_x10 + 5) / 10);
    x = draw_chip(x, HERO_CHIPS_Y, v, tr(" receive to screen"), TH_TEXT);
    snprintf(v, sizeof(v), "%u", it->last.lost);
    draw_chip(x, HERO_CHIPS_Y, v, i18n_plural(it->last.lost) ? tr(" frames lost") : tr(" frame lost"), it->last.lost ? TH_WARN : TH_TEXT);
    ay = HERO_CHIPS_Y + 42;
  }

  int x = HERO_X;
  play_button.w = quit_button.w = 0;
  if (other_running) {
    quit_button.x = x; quit_button.y = ay;
    quit_button.w = draw_action(x, ay, tr("Quit running game"), true);
  } else {
    play_button.x = x; play_button.y = ay;
    play_button.w = draw_action(x, ay, running ? tr("Resume") : tr("Play"), true);
    if (running) {
      x += play_button.w + 12;
      quit_button.x = x; quit_button.y = ay;
      quit_button.w = draw_action(x, ay, tr("Quit game"), false);
    }
  }
}

static bool button_hit(int tx, int ty, int bx, int by, int bw) {
  return bw > 0 && tx >= bx && tx <= bx + bw && ty >= by && ty <= by + ACTION_H;
}

int ui_library(PSERVER_DATA server, PAPP_LIST apps, const char *host_name, int *app_id) {
  PREVIEW_SCREEN("library");
  load_items(apps);
  if (item_count == 0)
    return LIBRARY_OPTIONS;

  /* the running game, else the one focused last time, else the first of the shelf */
  int wanted = server->currentGame ? server->currentGame : last_focus_id;
  int focus = index_of(wanted);
  items[focus].focus = 1.0f;

  float scroll = 0.0f;
  bool first_frame = true;
  int result = LIBRARY_BACK;

  ui_end();
  gui_touch_reset();

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

    /* touch: tap a cover to focus it, tap the focused cover or a button to act */
    int tapped = -1;
    bool tap_play = false, tap_quit = false;
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    int tx, ty;
    if (gui_touch_tap(&touch, &tx, &ty)) {
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
      tap_play = button_hit(tx, ty, play_button.x, play_button.y, play_button.w);
      tap_quit = button_hit(tx, ty, quit_button.x, quit_button.y, quit_button.w);
    }

    lib_item *cur = &items[focus];
    bool running = running_id != 0 && cur->id == running_id;
    bool other_running = running_id != 0 && !running;

    if (tapped >= 0 && tapped != focus) {
      focus = tapped;
    } else if (((buttons & config.btn_confirm) && !hold) || tapped == focus || tap_play) {
      if (other_running) {
        // Offer to quit the running game and start this one (NowaLone/vita-moonlight).
        const char *running_name = "?";
        for (int i = 0; i < item_count; i++)
          if (items[i].id == running_id)
            running_name = items[i].name;
        char question[320];
        snprintf(question, sizeof(question), tr("%s is running on the PC.\nQuit it and start %s?"),
                 running_name, cur->name);
        if (display_confirm(question)) {
          *app_id = cur->id;
          result = LIBRARY_SWITCH;
          break;
        }
        gui_touch_reset();
      } else {
        *app_id = cur->id;
        result = LIBRARY_LAUNCH;
        break;
      }
    } else if ((((buttons & SCE_CTRL_SQUARE) && !hold) || tap_quit) && running_id != 0) {
      result = LIBRARY_QUIT_APP;
      break;
    } else if ((buttons & SCE_CTRL_SELECT) && !hold) {
      /* favourite or not: the shelf takes its new order, the focus stays on the game */
      int id = cur->id;
      cur->favourite = !cur->favourite;
      set_favourite(id, cur->favourite);
      sort_items();
      focus = index_of(id);
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
    scroll = first_frame ? scroll_target : scroll + (scroll_target - scroll) * 0.25f;
    first_frame = false;

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
    draw_hero(cur, running, other_running);

    char counter[32];
    snprintf(counter, sizeof(counter), "%d / %d", focus + 1, item_count);
    theme_text_right(TH_RIGHT, SHELF_BOTTOM - COVER_FH - 14, TH_TEXT3, TS_SMALL, counter);

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

    theme_hint_item hints[6];
    int n = 0;
    if (other_running) {
      hints[n++] = (theme_hint_item){GLYPH_SQUARE, tr("Quit running game")};
    } else {
      hints[n++] = (theme_hint_item){theme_confirm_glyph(), running ? tr("Resume") : tr("Play")};
      if (running)
        hints[n++] = (theme_hint_item){GLYPH_SQUARE, tr("Quit game")};
    }
    hints[n++] = (theme_hint_item){GLYPH_SELECT, cur->favourite ? tr("Unfavourite") : tr("Favourite")};
    hints[n++] = (theme_hint_item){GLYPH_TRIANGLE, tr("Options")};
    hints[n++] = (theme_hint_item){GLYPH_START, tr("Settings")};
    hints[n++] = (theme_hint_item){theme_cancel_glyph(), tr("Back")};
    theme_hints(hints, n);
    ui_end();
  }

  last_focus_id = items[focus].id;
  free_items();
  return result;
}
