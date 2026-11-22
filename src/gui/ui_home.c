/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Home screen; see ui_home.h and home_logic.h (where the cursor goes, the
 * file each PC's library leaves for this screen).
 *
 * Layout (960x544): the top bar; the side menu on the left (x 32-204); a thin
 * divider; on the right (x 240-912, y 82-476) the preview of the menu entry
 * under the cursor. On Home: a large card (the game to pick up, or what the
 * PC needs) and under it a row of covers (recent games, favourites, "See all").
 * The right arrow enters any preview that has something to pick; one element
 * at a time is selected, lifted, lit and outlined in the theme's colour.
 *
 * Kept light: the PC files and the summaries of the settings and the help are
 * read when the screen opens or comes back, not while it is shown; covers are
 * loaded one per frame, at most COVER_SLOTS, and freed before any other screen
 * (a stream needs the memory).
 */
#include "ui_home.h"
#include "home_logic.h"
#include "home_data.h"
#include "cover_look.h"
#include "library_logic.h"
#include "boxart.h"
#include "cover_shape.h"
#include "guilib.h"
#include "theme.h"
#include "splash.h"
#include "preview_hooks.h"
#include "ui_connect.h"
#include "ui_device.h"
#include "ui_help.h"
#include "ui_settings.h"
#include "../config.h"
#include "../connection.h"
#include "../device.h"
#include "../check_host.h"
#include "../video/stream_stats.h"
#include "../log.h"
#include "../i18n.h"

#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- layout ---- */
#define MENU_X 32
#define MENU_W 172
#define MENU_Y 82
#define MENU_ROW_H 46
#define MENU_ROW_STEP 52
#define DIVIDER_X 218
#define AREA_X 240.0f
#define AREA_W 672.0f
#define AREA_Y 82.0f
#define AREA_H 394.0f                       /* down to TH_CONTENT_END */
#define MENU_QUIT_Y (AREA_Y + AREA_H - MENU_ROW_H)
#define GAP 14.0f
#define TILE_R 16.0f
#define PREVIEW_TOP 128.0f                  /* under a preview's title */
#define ICON_SIZE ICON_HOME_SIZE
/* Home: the large card, then the row of covers (the size of the Library page's
 * covers before 1.1.0-beta.4: 96 x 128 could not be read well on the Vita) */
#define HERO_H 160.0f
#define HERO_COVER_W 96.0f
#define HERO_COVER_H 128.0f
#define SLOT_W 120.0f
#define SLOT_H 160.0f
#define SLOT_Y (AREA_Y + HERO_H + 34)       /* 276 */
#define SLOT_GAP ((AREA_W - (ROW_GAMES + 1) * SLOT_W) / ROW_GAMES)
#define ROW_GAMES 4                         /* then "See all" */
/* Library preview: two rows of five covers */
#define SHELF_COLS 5
#define SHELF_SHOWN 10
#define SHELF_W 120.0f
#define SHELF_H 160.0f
#define SHELF_GAP_Y 16.0f
/* Computers preview */
#define PC_CARD_H 200.0f                    /* one PC */
#define PC_ROW_H 60.0f                      /* each of several PCs */
#define PC_SHOWN 4
#define CHOICE_H 54.0f
/* Settings and Help previews: cards in two rows */
#define CARD_H ((TH_CONTENT_END - PREVIEW_TOP - GAP) / 2)

/* ---- what the screen shows, read when it opens or comes back ---- */
#define CARD_TEXT 96
typedef struct {
  char value[CARD_TEXT];
  char line[3][CARD_TEXT];
  int warn;
} home_card;

typedef struct {
  bool connected;                 /* a stream is paused: the dashboard is about it */
  int host;                       /* index in known_devices of the PC shown, -1: none */
  int paired[MAX_HOSTS];          /* indexes of the paired PCs */
  int paired_count;
  bool has_games;                 /* its home.txt was read */
  home_games games;
  const home_game *resume;        /* the game to pick up, NULL: none known */
  bool has_last;                  /* its last session (stats/last_<id>.txt) */
  stream_last_session last;
  home_row_game row[ROW_GAMES];   /* the row under the large card */
  int row_count;
  int paused_id;                  /* the paused stream's game */
  char paused_name[128];
  home_card settings[SETTINGS_CAT_COUNT];
  home_card help[HELP_COUNT];
} home_view;

static home_view view;
static home_nav nav = {true, HOME_MENU_HOME, 0};
static char version_text[96];

/* The selection grows into place when it moves (0 to 1). */
static float lift = 1.0f;
static int lift_key = -1;

/* Host scan: the PCs' states. A PC not seen yet shows "Checking..." for the
 * few seconds a first pass takes, rather than "Offline". */
static SceUInt64 scan_since = 0;
static bool scan_settled = false;

/* ---------------- covers ---------------- */

#define COVER_SLOTS 20   /* the large card, the row and the library preview */
typedef struct {
  int id;
  vita2d_texture *tex;
  bool tried;
} home_cover;

static home_cover covers[COVER_SLOTS];
static int cover_count = 0;

static void covers_free(void) {
  for (int i = 0; i < cover_count; i++) {
    if (covers[i].tex)
      vita2d_free_texture(covers[i].tex);
  }
  memset(covers, 0, sizeof(covers));
  cover_count = 0;
}

/* The cover of a game if it is loaded; otherwise it is asked for (and loaded
 * by covers_load_one, one per frame). */
static vita2d_texture *cover_of(int id) {
  for (int i = 0; i < cover_count; i++)
    if (covers[i].id == id)
      return covers[i].tex;
  if (cover_count < COVER_SLOTS)
    covers[cover_count++] = (home_cover){id, NULL, false};
  return NULL;
}

static void covers_load_one(void) {
  if (!view.has_games || !view.games.covers[0])
    return;
  for (int i = 0; i < cover_count; i++) {
    if (covers[i].tried)
      continue;
    covers[i].tried = true;   /* once: no cover is downloaded while the home is shown */
    covers[i].tex = boxart_load_texture_from(view.games.covers, covers[i].id);
    return;
  }
}

/* ---------------- data ---------------- */

static const device_info_t *shown_host(void) {
  return view.host >= 0 && view.host < known_devices.count ? &known_devices.devices[view.host] : NULL;
}

/* "Label: value", as the Settings page names them. */
static void label_value(char *out, size_t n, int which) {
  char value[64];
  ui_settings_value(which, value, sizeof(value));
  snprintf(out, n, tr("%s: %s"), ui_settings_label(which), value);
}

/* What each card of the Settings preview says: read here, not every frame. */
static void load_settings_cards(void) {
  home_card *c;
  memset(view.settings, 0, sizeof(view.settings));

  c = &view.settings[SETTINGS_CAT_GENERAL];
  ui_settings_value(SETTINGS_SUM_LANGUAGE, c->value, sizeof(c->value));
  label_value(c->line[0], sizeof(c->line[0]), SETTINGS_SUM_THEME);
  label_value(c->line[1], sizeof(c->line[1]), SETTINGS_SUM_CONNECTION);

  c = &view.settings[SETTINGS_CAT_VIDEO];
  ui_settings_value(SETTINGS_SUM_RESOLUTION, c->value, sizeof(c->value));
  ui_settings_value(SETTINGS_SUM_FPS, c->line[0], sizeof(c->line[0]));
  ui_settings_value(SETTINGS_SUM_BITRATE, c->line[1], sizeof(c->line[1]));

  c = &view.settings[SETTINGS_CAT_PERFORMANCE];
  ui_settings_value(SETTINGS_SUM_PROFILE, c->value, sizeof(c->value));
  snprintf(c->line[0], sizeof(c->line[0]), "%s", ui_settings_label(SETTINGS_SUM_PROFILE));
  label_value(c->line[1], sizeof(c->line[1]), SETTINGS_SUM_LL_AUDIO);

  c = &view.settings[SETTINGS_CAT_CONTROLS];
  ui_settings_value(SETTINGS_SUM_CONTROLLER, c->value, sizeof(c->value));
  snprintf(c->line[0], sizeof(c->line[0]), "%s", ui_settings_label(SETTINGS_SUM_CONTROLLER));

  c = &view.settings[SETTINGS_CAT_TOUCH];
  ui_settings_value(SETTINGS_SUM_TOUCH, c->value, sizeof(c->value));
  snprintf(c->line[0], sizeof(c->line[0]), "%s", ui_settings_label(SETTINGS_SUM_TOUCH));
  label_value(c->line[1], sizeof(c->line[1]), SETTINGS_SUM_REAR_TOUCH);

  c = &view.settings[SETTINGS_CAT_ADVANCED];
  int changed = ui_settings_advanced_changed();
  if (changed == 0) {
    snprintf(c->value, sizeof(c->value), "%s", tr("Default"));
    label_value(c->line[0], sizeof(c->line[0]), SETTINGS_SUM_REMOTE);
  } else {
    snprintf(c->value, sizeof(c->value), "%s", tr("Changed"));
    snprintf(c->line[0], sizeof(c->line[0]), i18n_plural(changed) ? tr("%d options changed") : tr("%d option changed"),
             changed);
    label_value(c->line[1], sizeof(c->line[1]), SETTINGS_SUM_REMOTE);
  }

  c = &view.settings[SETTINGS_CAT_TESTS];
  const char *names[2] = {NULL, NULL};
  int on = ui_settings_tests_on(names, 2);
  if (on == 0) {
    snprintf(c->value, sizeof(c->value), "%s", tr("None"));
    snprintf(c->line[0], sizeof(c->line[0]), "%s", tr("As in 1.0"));
  } else {
    snprintf(c->value, sizeof(c->value), i18n_plural(on) ? tr("%d options on") : tr("%d option on"), on);
    for (int k = 0; k < 2; k++)
      if (names[k])
        snprintf(c->line[k], sizeof(c->line[k]), "%s", names[k]);
  }
}

static void load_help_cards(void) {
  ui_help_refresh();
  for (int t = 0; t < HELP_COUNT; t++) {
    home_card *c = &view.help[t];
    memset(c, 0, sizeof(*c));
    ui_help_card(t, c->value, sizeof(c->value), c->line[0], sizeof(c->line[0]), c->line[1], sizeof(c->line[1]),
                 &c->warn);
  }
}

static void load_view(void) {
  static home_games candidate;   /* menus thread only, kept off the stack */
  covers_free();
  memset(&view, 0, sizeof(view));
  view.host = -1;
  view.connected = ui_connect_connected();
  for (int i = 0; i < known_devices.count && i < MAX_HOSTS; i++)
    if (known_devices.devices[i].paired)
      view.paired[view.paired_count++] = i;

  if (view.connected) {
    /* the PC of the paused stream */
    view.paused_id = ui_connect_current_game_id();
    snprintf(view.paused_name, sizeof(view.paused_name), "%s", ui_connect_current_game_name());
    const char *label = ui_connect_host_label();
    for (int i = 0; i < known_devices.count && i < MAX_HOSTS; i++)
      if (!strcmp(known_devices.devices[i].name, label))
        view.host = i;
    if (view.host >= 0)
      view.has_games = home_data_load(known_devices.devices[view.host].name, &view.games);
  } else {
    /* the PC whose library was left last, else the first paired one */
    const char *opened[MAX_HOSTS];
    char stamps[MAX_HOSTS][20];
    for (int k = 0; k < view.paired_count; k++) {
      int i = view.paired[k];
      stamps[k][0] = 0;
      if (home_data_load(known_devices.devices[i].name, &candidate))
        snprintf(stamps[k], sizeof(stamps[k]), "%s", candidate.opened[0] ? candidate.opened : "0");
      opened[k] = stamps[k];
    }
    int best = home_pick_host(opened, view.paired_count);
    if (best < 0 && view.paired_count > 0)
      best = 0;
    if (best >= 0) {
      view.host = view.paired[best];
      view.has_games = home_data_load(known_devices.devices[view.host].name, &view.games);
    }
  }

  if (view.has_games && view.games.recent_count > 0 && !view.connected)
    view.resume = &view.games.recent[0];
  if (view.resume)
    view.has_last = stream_stats_read_last(view.resume->id, &view.last);
  if (view.has_games) {
    int skip = view.connected ? view.paused_id : (view.resume ? view.resume->id : 0);
    view.row_count = home_row_games(&view.games, skip, view.row, ROW_GAMES);
  }
  load_settings_cards();
  load_help_cards();
}

/* State of the PC shown: HOST_STATE_*, or -1 while the first scan has not
 * seen it yet. */
static int shown_state(void) {
  if (view.host < 0)
    return HOST_STATE_OFFLINE;
  int s = ui_host_state(view.host);
  return s == HOST_STATE_OFFLINE && !scan_settled ? -1 : s;
}

static void scan_start(void) {
  if (view.connected)
    return;
  bool running = g_host_scan_thread_status == 1;
  start_host_scan_thread();
  if (!running && g_host_scan_thread_status == 1) {
    scan_since = sceKernelGetProcessTimeWide();
    scan_settled = false;
  }
}

static void scan_check(void) {
  if (scan_settled || view.connected)
    return;
  SceUInt64 now = sceKernelGetProcessTimeWide();
  if (g_host_scan_thread_status != 1) {
    /* a scan asked to stop may still be ending (a ping in flight): start it
     * once it has, trying again every second */
    static SceUInt64 last_try = 0;
    if (now - last_try >= 1000000) {
      last_try = now;
      scan_start();
    }
    return;
  }
  if (g_host_status_changed) {
    scan_settled = true;
    return;
  }
  /* about one second of mDNS and a ping per PC */
  SceUInt64 wait_us = (2500 + 1500 * (SceUInt64)(view.paired_count > 4 ? 4 : view.paired_count)) * 1000;
  if (now - scan_since > wait_us)
    scan_settled = true;
}

/* ---------------- text ---------------- */

static bool moment_now(const char *when, lib_moment *then, lib_moment *today) {
  SceDateTime now;
  if (!lib_parse_moment(when, then) || sceRtcGetCurrentClockLocalTime(&now) < 0)
    return false;
  *today = (lib_moment){now.year, now.month, now.day, now.hour, now.minute};
  return true;
}

/* "Today at 19:46", "3 days ago"... from a "YYYY-MM-DD HH:MM" moment. */
static void describe_when(const char *when, char *out, size_t n) {
  lib_moment then, today;
  out[0] = 0;
  if (!moment_now(when, &then, &today)) {
    snprintf(out, n, "%s", when ? when : "");
    return;
  }
  int days = 0;
  switch (lib_when(&then, &today, &days)) {
    case LIB_WHEN_TODAY: snprintf(out, n, tr("today at %02d:%02d"), then.hour, then.minute); break;
    case LIB_WHEN_YESTERDAY: snprintf(out, n, tr("yesterday at %02d:%02d"), then.hour, then.minute); break;
    case LIB_WHEN_DAYS_AGO: snprintf(out, n, tr("%d days ago"), days); break;
    default: snprintf(out, n, tr("on %02d/%02d/%04d"), then.day, then.month, then.year); break;
  }
}

/* The same, short enough for the width of a cover: "Today", "3 d ago", "2 wk ago". */
static void describe_when_short(const char *when, char *out, size_t n) {
  lib_moment then, today;
  out[0] = 0;
  if (!when || !when[0] || !moment_now(when, &then, &today))
    return;
  int days = 0;
  switch (lib_when(&then, &today, &days)) {
    case LIB_WHEN_TODAY: snprintf(out, n, "%s", tr("Today")); return;
    case LIB_WHEN_YESTERDAY: snprintf(out, n, "%s", tr("Yesterday")); return;
    case LIB_WHEN_DAYS_AGO: snprintf(out, n, tr("%d d ago"), days); return;
    default: break;
  }
  if (days >= 7 && days < 31)
    snprintf(out, n, tr("%d wk ago"), days / 7);
  else if (days >= 31 && days < 365)
    snprintf(out, n, i18n_plural((unsigned)(days / 30)) ? tr("%d months ago") : tr("%d month ago"), days / 30);
  else
    snprintf(out, n, "%02d/%02d/%02d", then.day, then.month, then.year % 100);
}

static void describe_length(unsigned seconds, char *out, size_t n) {
  unsigned hours, minutes;
  lib_duration_parts(seconds, &hours, &minutes);
  if (hours)
    snprintf(out, n, tr("%u h %02u"), hours, minutes);
  else
    snprintf(out, n, tr("%u min"), minutes);
}

/* ---------------- drawing helpers ---------------- */

static void draw_icon(const char *icon, float x, float cy, unsigned int color) {
  if (font)
    vita2d_font_draw_text(font, (int)(x + 0.5f), (int)(cy + ICON_SIZE * 0.36f + 0.5f), theme_color(color), ICON_SIZE,
                          icon);
}

static void draw_icon_centered(const char *icon, float cx, float cy, unsigned int color) {
  if (font)
    draw_icon(icon, cx - vita2d_font_text_width(font, ICON_SIZE, icon) / 2.0f, cy, color);
}

/* The halo around what is selected: three soft rings in the theme's colour. */
static void halo(float x, float y, float w, float h, float r) {
  for (int k = 3; k >= 1; k--) {
    float o = 2.0f * k + 1.0f;
    theme_rounded_outline(x - o, y - o, w + 2 * o, h + 2 * o, r + o, theme_alpha(TH_ACCENT, (unsigned)(26 * (4 - k) * lift)));
  }
}

/* A card; the selected one is lifted (a little larger), lit, with a thick
 * border in the theme's colour and a halo. */
static void card_box(float x, float y, float w, float h, float r, bool focused) {
  if (!focused) {
    theme_rounded_rect(x, y, w, h, r, TH_PANEL);
    theme_rounded_outline(x, y, w, h, r, TH_PANEL_EDGE);
    return;
  }
  float e = 3.0f * lift;
  x -= e;
  y -= e;
  w += 2 * e;
  h += 2 * e;
  r += e;
  halo(x, y, w, h, r);
  theme_rounded_rect(x, y, w, h, r, TH_PANEL);
  theme_rounded_rect(x, y, w, h, r, theme_alpha(TH_ACCENT, 34));
  theme_rounded_outline(x, y, w, h, r, TH_ACCENT);
  theme_rounded_outline(x + 1, y + 1, w - 2, h - 2, r - 1, TH_ACCENT);
}

/* A game's cover, or its coloured tile with the initials until it is downloaded. */
static void draw_cover(const home_game *g, float x, float y, float w, float h, int initials_size, bool focused) {
  float r = cover_radius(w);
  if (focused)
    theme_cover_ring(x, y, w, h, r);
  else
    theme_rounded_rect(x + 2, y + 5, w, h, r, RGBA8(0, 0, 0, 90));
  vita2d_texture *tex = cover_of(g->id);
  if (tex) {
    /* the corners of a loaded cover are already transparent (cover_shape.c) */
    vita2d_draw_texture_tint_scale(tex, x, y, w / BOXART_W, h / BOXART_H, theme_color(RGBA8(255, 255, 255, 255)));
    return;
  }
  theme_rounded_rect(x, y, w, h, r, cover_tint(g->id));
  char initials[4];
  cover_initials(g->name, initials);
  theme_text(x + 10, y + initials_size + 8, RGBA8(255, 255, 255, 225), initials_size, initials);
  if (w >= 90) {
    theme_rounded_rect(x + 5, y + h - 27, w - 10, 22, 7, theme_alpha(TH_INK, 120));
    theme_text_fit(x + 11, y + h - 11, TH_TEXT, TS_MICRO, g->name, (int)w - 22);
  }
}

/* A cover that can be selected: when it is, it grows a little and gets a
 * frame (a light ring, set off by a dark line) and a halo. */
static void draw_pick_cover(const home_game *g, float x, float y, float w, float h, int initials_size, bool focused) {
  if (focused) {
    float ex = 4.0f * lift, ey = ex * h / w;
    x -= ex;
    y -= ey;
    w += 2 * ex;
    h += 2 * ey;
    float o = THEME_COVER_RING;
    halo(x - o, y - o, w + 2 * o, h + 2 * o, cover_radius(w) + o);
  }
  draw_cover(g, x, y, w, h, initials_size, focused);
}

/* A filled (primary) or outlined button with an optional icon. */
static void draw_button(float x, float y, float w, float h, const char *icon, const char *label, bool primary) {
  if (primary) {
    theme_rounded_rect(x, y, w, h, h / 2, TH_ACCENT);
  } else {
    theme_rounded_rect(x, y, w, h, h / 2, TH_FILL_SOFT);
    theme_rounded_outline(x, y, w, h, h / 2, RGBA8(255, 255, 255, 110));
  }
  unsigned int ink = primary ? TH_INK : TH_TEXT;
  int tw = theme_text_width(TS_HINT, label);
  float iw = icon && font ? vita2d_font_text_width(font, ICON_SIZE, icon) + 10 : 0;
  float left = x + (w - tw - iw) / 2;
  if (left < x + 12)
    left = x + 12;
  if (iw > 0)
    draw_icon(icon, left, y + h / 2, ink);
  theme_text_fit(left + iw, y + h / 2 + TS_HINT * 0.36f, ink, TS_HINT, label, (int)(x + w - 12 - left - iw));
}

/* Text cut into at most max_lines lines of max_w; returns the y after it. */
static float draw_wrapped(float x, float y, int size, unsigned int color, const char *text, int max_w, int max_lines,
                          float line_h) {
  char lines[4][256];
  if (max_lines > 4)
    max_lines = 4;
  int n = theme_wrap(size, text, max_w, lines, max_lines);
  for (int i = 0; i < n; i++)
    theme_text_fit(x, y + i * line_h, color, size, lines[i], max_w);
  return y + n * line_h;
}

static void state_line(float x, float baseline, int state, const char *paused_text) {
  unsigned int dot;
  const char *text;
  if (paused_text) {
    dot = TH_ACCENT;
    text = paused_text;
  } else if (state < 0) {
    dot = TH_TEXT3;
    text = tr("Checking...");
  } else {
    dot = state == HOST_STATE_ONLINE ? TH_OK : state == HOST_STATE_OFFLINE ? TH_ERR : TH_WARN;
    text = ui_host_state_text(state);
  }
  theme_circle(x + 4, baseline - 4, 4, dot);
  theme_text(x + 14, baseline, TH_TEXT3, TS_SMALL, text);
}

/* ---------------- the side menu ---------------- */

static const char *const k_menu_icons[HOME_MENU_COUNT] = {ICON_HOME, ICON_GRID, ICON_DESKTOP, ICON_COG, ICON_HELP,
                                                           ICON_POWER};

static const char *menu_label(int m) {
  switch (m) {
    case HOME_MENU_HOME: return tr("Home");
    case HOME_MENU_LIBRARY: return tr("Library");
    case HOME_MENU_COMPUTERS: return tr("Computers");
    case HOME_MENU_SETTINGS: return tr("Settings");
    case HOME_MENU_HELP: return tr("Help");
    default: return tr("Quit");
  }
}

static float menu_row_y(int m) {
  return m == HOME_MENU_QUIT ? MENU_QUIT_Y : MENU_Y + m * MENU_ROW_STEP;
}

static void draw_menu(void) {
  for (int m = 0; m < HOME_MENU_COUNT; m++) {
    float y = menu_row_y(m);
    bool focus = !nav.in_panel && nav.menu == m;
    bool current = nav.in_panel && nav.menu == m;
    if (focus) {
      theme_rounded_rect(MENU_X, y, MENU_W, MENU_ROW_H, 12, theme_alpha(TH_ACCENT, 38));
      theme_rounded_outline(MENU_X, y, MENU_W, MENU_ROW_H, 12, TH_ACCENT);
      theme_rounded_outline(MENU_X + 1, y + 1, MENU_W - 2, MENU_ROW_H - 2, 11, TH_ACCENT);
    } else if (current) {
      theme_rounded_rect(MENU_X, y, MENU_W, MENU_ROW_H, 12, TH_FILL_FOCUS);
    }
    bool lit = focus || current;
    draw_icon(k_menu_icons[m], MENU_X + 14, y + MENU_ROW_H / 2.0f, lit ? TH_TEXT : TH_TEXT3);
    theme_text_fit(MENU_X + 46, y + MENU_ROW_H / 2.0f + TS_HINT * 0.36f, lit ? TH_TEXT : TH_TEXT2, TS_HINT,
                   menu_label(m), MENU_W - 54);
  }
  theme_rect(DIVIDER_X, AREA_Y, 1, AREA_H, TH_DIVIDER);
}

/* ---------------- the panels: what can be selected, and where ---------------- */

typedef struct { float x, y, w, h; } rect;

static bool inside(int tx, int ty, rect r) {
  return tx >= r.x && tx <= r.x + r.w && ty >= r.y && ty <= r.y + r.h;
}

/* What the large card of Home does now. */
typedef enum { HERO_ADD, HERO_PAUSED, HERO_WAKE, HERO_OPTIONS, HERO_HOME, HERO_AWAY, HERO_CONTINUE, HERO_LIBRARY } hero_kind;

static hero_kind hero_now(void) {
  if (view.host < 0 && !view.connected)
    return HERO_ADD;
  if (view.connected)
    return HERO_PAUSED;
  int state = shown_state();
  /* Wake-on-LAN only reaches the network the Vita is on: at home */
  if (state == HOST_STATE_OFFLINE && !config.away)
    return shown_host() && shown_host()->mac[0] ? HERO_WAKE : HERO_OPTIONS;
  if (state == HOST_STATE_AT_HOME)
    return HERO_HOME;
  if (state == HOST_STATE_AWAY_ONLY)
    return HERO_AWAY;
  if (state == HOST_STATE_OFFLINE || state == HOST_STATE_MOVED || state == HOST_STATE_NO_ADDRESS)
    return HERO_OPTIONS;
  return view.resume ? HERO_CONTINUE : HERO_LIBRARY;
}

static int pcs_shown(void) {
  if (view.connected)
    return view.host >= 0 ? 1 : 0;
  return view.paired_count > PC_SHOWN ? PC_SHOWN : view.paired_count;
}

static float pcs_bottom(void) {
  int n = pcs_shown();
  if (n == 0)
    return PREVIEW_TOP + 70;   /* a line saying there is none */
  return n == 1 ? PREVIEW_TOP + PC_CARD_H : PREVIEW_TOP + n * (PC_ROW_H + 10) - 10;
}

static float slot_x(int k) { return AREA_X + k * (SLOT_W + SLOT_GAP); }

/* The panel of a menu entry, and the place of each item (for drawing and touch). */
static void panel_layout(int menu, home_panel *p, rect *where) {
  memset(p, 0, sizeof(*p));
  int i;
  switch (menu) {
    case HOME_MENU_HOME:
      i = home_panel_add(p, 0, 0, 6);
      if (view.host < 0 && !view.connected) {
        where[i] = (rect){AREA_X, AREA_Y, AREA_W, AREA_H};   /* "Add a computer" */
        break;
      }
      where[i] = (rect){AREA_X, AREA_Y, AREA_W, HERO_H};
      for (int k = 0; k < view.row_count; k++) {
        i = home_panel_add(p, 1, k, 1);
        where[i] = (rect){slot_x(k), SLOT_Y, SLOT_W, SLOT_H};
      }
      i = home_panel_add(p, 1, ROW_GAMES, 1);
      where[i] = (rect){slot_x(ROW_GAMES), SLOT_Y, SLOT_W, SLOT_H};
      break;
    case HOME_MENU_LIBRARY:
      if (!shown_host() || !view.has_games)
        break;
      for (int k = 0; k < view.games.shelf_count && k < SHELF_SHOWN; k++) {
        float gap = (AREA_W - SHELF_COLS * SHELF_W) / (SHELF_COLS - 1);
        i = home_panel_add(p, k / SHELF_COLS, k % SHELF_COLS, 1);
        where[i] = (rect){AREA_X + (k % SHELF_COLS) * (SHELF_W + gap),
                          PREVIEW_TOP + (k / SHELF_COLS) * (SHELF_H + SHELF_GAP_Y), SHELF_W, SHELF_H};
      }
      break;
    case HOME_MENU_COMPUTERS: {
      int n = pcs_shown();
      for (int k = 0; k < n; k++) {
        i = home_panel_add(p, k, 0, 2);
        where[i] = n == 1 ? (rect){AREA_X, PREVIEW_TOP, AREA_W, PC_CARD_H}
                          : (rect){AREA_X, PREVIEW_TOP + k * (PC_ROW_H + 10), AREA_W, PC_ROW_H};
      }
      if (view.connected)
        break;   /* nothing else while a stream is paused, as on the Computers page */
      float y = pcs_bottom() + 16, w = (AREA_W - GAP) / 2;
      i = home_panel_add(p, n, 0, 1);
      where[i] = (rect){AREA_X, y, w, CHOICE_H};
      i = home_panel_add(p, n, 1, 1);
      where[i] = (rect){AREA_X + w + GAP, y, w, CHOICE_H};
      break;
    }
    case HOME_MENU_SETTINGS: {
      float w = (AREA_W - 3 * GAP) / 4;
      for (int c = 0; c < SETTINGS_CAT_COUNT; c++) {
        int row = c < 4 ? 0 : 1, col = c < 4 ? c : c - 4;
        i = home_panel_add(p, row, col, c == SETTINGS_CAT_TESTS ? 2 : 1);
        where[i] = (rect){AREA_X + col * (w + GAP), PREVIEW_TOP + row * (CARD_H + GAP),
                          c == SETTINGS_CAT_TESTS ? 2 * w + GAP : w, CARD_H};
      }
      break;
    }
    case HOME_MENU_HELP: {
      float w = (AREA_W - GAP) / 2;
      for (int t = 0; t < HELP_COUNT; t++) {
        i = home_panel_add(p, t / 2, t % 2, 1);
        where[i] = (rect){AREA_X + (t % 2) * (w + GAP), PREVIEW_TOP + (t / 2) * (CARD_H + GAP), w, CARD_H};
      }
      break;
    }
    default:
      break;
  }
}

static home_panel panel;                 /* of nav.menu, laid out every frame */
static rect panel_rect[HOME_PANEL_MAX];

static void layout_now(void) {
  panel_layout(nav.menu, &panel, panel_rect);
}

static bool selected(int menu, int item) {
  return nav.in_panel && nav.menu == menu && nav.item == item;
}

/* ---------------- Home: the large card and the row ---------------- */

static void draw_hero(bool focused) {
  rect r = {AREA_X, AREA_Y, AREA_W, HERO_H};
  hero_kind kind = hero_now();
  if (kind == HERO_ADD) {
    /* no PC yet: one card over the whole area */
    r.h = AREA_H;
    card_box(r.x, r.y, r.w, r.h, TILE_R, focused);
    theme_text(r.x + 32, r.y + 64, TH_ACCENT, TS_SMALL, tr("Welcome"));
    theme_text_fit(r.x + 32, r.y + 104, TH_TEXT, TS_TITLE, tr("Add a computer"), (int)r.w - 64);
    draw_wrapped(r.x + 32, r.y + 146, TS_HINT, TH_TEXT2,
                 tr("VitaMoon plays the games of a PC that runs Sunshine. Add yours: search for it on the Wi-Fi or type its address."),
                 (int)r.w - 64, 3, 24);
    draw_button(r.x + 32, r.y + r.h - 32 - 46, 260, 46, ICON_DESKTOP, tr("Add a computer"), true);
    return;
  }

  card_box(r.x, r.y, r.w, r.h, TILE_R, focused);
  const device_info_t *host = shown_host();
  const char *host_name = host ? host->name : ui_connect_host_label();
  float cover_x = r.x + 18, cover_y = r.y + (HERO_H - HERO_COVER_H) / 2, cw = HERO_COVER_W, ch = HERO_COVER_H;
  float tx = cover_x + cw + 22, tw = r.x + r.w - 22 - tx;
  const char *tag = "", *title = "", *button = "", *icon = NULL;
  char title_buf[160] = "", meta[2][200] = {"", ""};
  home_game paused_game;
  const home_game *game = NULL;
  unsigned int tag_color = TH_ACCENT;

  switch (kind) {
    case HERO_PAUSED:
      memset(&paused_game, 0, sizeof(paused_game));
      paused_game.id = view.paused_id;
      snprintf(paused_game.name, sizeof(paused_game.name), "%.95s", view.paused_name);
      game = &paused_game;
      tag = tr("Paused");
      title = view.paused_name[0] ? view.paused_name : ui_connect_host_label();
      snprintf(meta[0], sizeof(meta[0]), tr("Still running on %s"), ui_connect_host_label());
      button = tr("Resume");
      icon = ICON_PLAY;
      break;
    case HERO_WAKE:
      tag = tr("PC off");
      tag_color = TH_TEXT3;
      snprintf(title_buf, sizeof(title_buf), tr("%s is off"), host_name);
      title = title_buf;
      if (view.resume)
        snprintf(meta[0], sizeof(meta[0]), tr("Wake-on-LAN ready · last game: %s"), view.resume->name);
      else
        snprintf(meta[0], sizeof(meta[0]), "%s", tr("Wake-on-LAN ready"));
      button = tr("Wake the PC");
      icon = ICON_POWER;
      break;
    case HERO_OPTIONS:
    case HERO_HOME:
    case HERO_AWAY: {
      int state = shown_state();
      tag = ui_host_state_text(state);
      tag_color = state == HOST_STATE_OFFLINE ? TH_TEXT3 : TH_WARN;
      title = host_name;
      snprintf(meta[0], sizeof(meta[0]), "%s", ui_host_advice_short(state));
      button = kind == HERO_HOME ? tr("Switch to At home") : kind == HERO_AWAY ? tr("Switch to Away") : tr("PC options");
      icon = kind == HERO_HOME ? ICON_HOME : NULL;
      game = view.resume;
      break;
    }
    case HERO_CONTINUE: {
      game = view.resume;
      tag = tr("Continue");
      title = game->name;
      char when[64], length[32];
      describe_when(view.has_last ? view.last.when : game->when, when, sizeof(when));
      if (view.has_last) {
        describe_length(view.last.seconds, length, sizeof(length));
        snprintf(meta[0], sizeof(meta[0]), tr("on %s · %s · %s"), host_name, when, length);
      } else {
        snprintf(meta[0], sizeof(meta[0]), tr("on %s · %s"), host_name, when);
      }
      button = tr("Continue");
      icon = ICON_PLAY;
      break;
    }
    default:
      tag = tr("Library");
      title = host_name;
      snprintf(meta[0], sizeof(meta[0]), "%s", tr("Open the library once: your games will show up here."));
      button = tr("Open the library");
      icon = ICON_GRID;
      break;
  }

  if (game && (game->id || game->name[0])) {
    draw_cover(game, cover_x, cover_y, cw, ch, TS_TITLE, false);
  } else {
    theme_rounded_rect(cover_x, cover_y, cw, ch, cover_radius(cw), TH_FILL_SOFT);
    theme_rounded_outline(cover_x, cover_y, cw, ch, cover_radius(cw), TH_DIVIDER);
    draw_icon_centered(kind == HERO_LIBRARY ? ICON_GRID : ICON_DESKTOP, cover_x + cw / 2, cover_y + ch / 2 - 8,
                       TH_TEXT3);
    if (kind != HERO_LIBRARY)
      theme_text_fit_center(cover_x + cw / 2, cover_y + ch / 2 + 28, TH_TEXT3, TS_MICRO, host_name, (int)cw - 12);
  }

  theme_text_fit(tx, r.y + 36, tag_color, TS_SMALL, tag, (int)tw);
  theme_text_fit(tx, r.y + 70, TH_TEXT, TS_TITLE, title, (int)tw);
  draw_wrapped(tx, r.y + 94, TS_SMALL, TH_TEXT3, meta[0], (int)tw, 1, 18);
  float bw = theme_text_width(TS_HINT, button) + (icon ? 70 : 52);
  if (bw < 210)
    bw = 210;
  if (bw > tw)
    bw = tw;
  draw_button(tx, r.y + HERO_H - 16 - 42, bw, 42, icon, button, focused);
}

static const char *row_kind_label(int kind) {
  switch (kind) {
    case HOME_ROW_RECENT: return tr("Recent");
    case HOME_ROW_FAVOURITE: return tr("Favourites");
    default: return tr("Games");
  }
}

static void draw_row(void) {
  int focus = nav.in_panel && nav.menu == HOME_MENU_HOME ? nav.item : -1;
  float label_y = SLOT_Y - 12, under_y = SLOT_Y + SLOT_H + 24;
  if (view.row_count == 0) {
    draw_wrapped(slot_x(0), SLOT_Y + 50, TS_HINT, TH_TEXT2, tr("Open the library once: your games will show up here."),
                 (int)(slot_x(ROW_GAMES) - slot_x(0) - SLOT_GAP), 2, 22);
  }
  for (int k = 0; k < view.row_count; k++) {
    float x = slot_x(k);
    int kind = view.row[k].kind;
    if (k == 0 || view.row[k - 1].kind != kind) {
      theme_text_fit(x, label_y, TH_TEXT3, TS_SMALL, row_kind_label(kind), (int)(AREA_X + AREA_W - x));
      if (k > 0)
        theme_rect(x - SLOT_GAP / 2, label_y - 12, 1, under_y - label_y + 14, TH_DIVIDER);
    }
    bool on = focus == 1 + k;
    char when[48];
    describe_when_short(view.row[k].game->when, when, sizeof(when));
    if (when[0])
      theme_text_fit(x, under_y, on ? TH_TEXT : TH_TEXT3, TS_SMALL, when, (int)(SLOT_W + SLOT_GAP - 4));
  }
  /* the covers, the selected one last so that its ring is over its neighbours */
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < view.row_count; k++)
      if ((focus == 1 + k) == (pass == 1))
        draw_pick_cover(view.row[k].game, slot_x(k), SLOT_Y, SLOT_W, SLOT_H, TS_LEAD, pass == 1);

  /* "See all" */
  float x = slot_x(ROW_GAMES);
  bool all = focus == 1 + view.row_count;
  if (all) {
    card_box(x, SLOT_Y, SLOT_W, SLOT_H, 12, true);
  } else {
    theme_rounded_rect(x, SLOT_Y, SLOT_W, SLOT_H, 12, TH_FILL_SOFT);
    theme_rounded_outline(x, SLOT_Y, SLOT_W, SLOT_H, 12, TH_DIVIDER);
  }
  draw_icon_centered(ICON_GRID, x + SLOT_W / 2, SLOT_Y + SLOT_H / 2 - 12, all ? TH_TEXT : TH_TEXT2);
  theme_text_fit_center(x + SLOT_W / 2, SLOT_Y + SLOT_H / 2 + 26, all ? TH_TEXT : TH_TEXT2, TS_SMALL, tr("See all"),
                        (int)SLOT_W - 8);
  if (view.has_games) {
    char count[48];
    snprintf(count, sizeof(count), i18n_plural(view.games.total) ? tr("%d games") : tr("%d game"), view.games.total);
    theme_text_fit(x, under_y, TH_TEXT3, TS_SMALL, count, (int)SLOT_W);
  }
}

static void draw_dashboard(void) {
  draw_hero(selected(HOME_MENU_HOME, 0));
  if (view.host < 0 && !view.connected)
    return;
  draw_row();
}

/* ---------------- the other previews ---------------- */

static void preview_title(const char *title, const char *sub) {
  theme_text(AREA_X, AREA_Y + 22, TH_TEXT, TS_LEAD, title);
  if (sub && sub[0]) {
    int tw = theme_text_width(TS_LEAD, title);
    theme_text_fit(AREA_X + tw + 12, AREA_Y + 22, TH_TEXT3, TS_SMALL, sub, (int)(AREA_W - tw - 12));
  }
}

static void draw_library_preview(void) {
  const device_info_t *host = shown_host();
  char sub[200] = "";
  if (host && view.has_games) {
    char count[48];
    snprintf(count, sizeof(count), i18n_plural(view.games.total) ? tr("%d games") : tr("%d game"), view.games.total);
    snprintf(sub, sizeof(sub), "%.120s · %s", host->name, count);
  } else if (host) {
    snprintf(sub, sizeof(sub), "%.120s", host->name);
  }
  preview_title(tr("Library"), sub);
  if (!host) {
    draw_wrapped(AREA_X, PREVIEW_TOP + 24, TS_HINT, TH_TEXT2, tr("Add a computer first: its games will show up here."),
                 (int)AREA_W, 2, 24);
    return;
  }
  if (!view.has_games || view.games.shelf_count == 0) {
    draw_wrapped(AREA_X, PREVIEW_TOP + 24, TS_HINT, TH_TEXT2, tr("Open the library once: your games will show up here."),
                 (int)AREA_W, 2, 24);
    return;
  }
  /* the selected cover last, so that its ring is over its neighbours */
  int on = nav.in_panel && nav.menu == HOME_MENU_LIBRARY ? nav.item : -1;
  for (int k = 0; k < panel.count; k++)
    if (k != on)
      draw_pick_cover(&view.games.shelf[k], panel_rect[k].x, panel_rect[k].y, SHELF_W, SHELF_H, TS_LEAD, false);
  if (on >= 0 && on < panel.count)
    draw_pick_cover(&view.games.shelf[on], panel_rect[on].x, panel_rect[on].y, SHELF_W, SHELF_H, TS_LEAD, true);
}

static void detail_row(float x, float y, float w, const char *label, const char *value) {
  theme_text(x, y, TH_TEXT3, TS_SMALL, label);
  theme_text_fit_right(x + w, y, TH_TEXT, TS_SMALL, value, (int)(w - 140));
  theme_rect(x, y + 10, w, 1, TH_DIVIDER);
}

/* An action under the PCs: outlined, or lifted when selected. */
static void draw_choice(rect r, const char *icon, const char *label, bool focused) {
  if (focused) {
    card_box(r.x, r.y, r.w, r.h, 14, true);
  } else {
    theme_rounded_rect(r.x, r.y, r.w, r.h, 14, TH_FILL_SOFT);
    theme_rounded_outline(r.x, r.y, r.w, r.h, 14, TH_DIVIDER);
  }
  draw_icon(icon, r.x + 18, r.y + r.h / 2, focused ? TH_TEXT : TH_TEXT2);
  theme_text_fit(r.x + 54, r.y + r.h / 2 + TS_HINT * 0.36f, focused ? TH_TEXT : TH_TEXT2, TS_HINT, label,
                 (int)r.w - 66);
}

static void draw_computers_preview(void) {
  char sub[64];
  snprintf(sub, sizeof(sub), i18n_plural(view.paired_count) ? tr("%d paired PCs") : tr("%d paired PC"),
           view.paired_count);
  preview_title(tr("Computers"), sub);
  int n = pcs_shown();
  for (int k = 0; k < n; k++) {
    int i = view.connected ? view.host : view.paired[k];
    const device_info_t *d = &known_devices.devices[i];
    rect r = panel_rect[k];
    bool on = selected(HOME_MENU_COMPUTERS, k);
    card_box(r.x, r.y, r.w, r.h, 14, on);
    int state = view.connected ? 0 : ui_host_state(i);
    if (!view.connected && state == HOST_STATE_OFFLINE && !scan_settled)
      state = -1;
    const char *paused = view.connected ? tr("Stream paused") : NULL;
    char address[320];
    if (config.away && !route_internet_address_usable(d->external))
      snprintf(address, sizeof(address), "%s", tr("Not known yet"));
    else
      snprintf(address, sizeof(address), "%s : %d", route_address(d, config.away), d->port);
    if (n == 1) {
      draw_icon(ICON_DESKTOP, r.x + 24, r.y + 46, TH_TEXT);
      theme_text_fit(r.x + 24, r.y + 104, TH_TEXT, TS_LEAD, d->name, 200);
      state_line(r.x + 24, r.y + 134, state, paused);
      float dx = r.x + 250, dw = r.w - 250 - 24;
      char stream[64];
      snprintf(stream, sizeof(stream), "%d x %d, %d fps", config.stream.width, config.stream.height, config.stream.fps);
      detail_row(dx, r.y + 52, dw, config.away ? tr("Internet address") : tr("Address"), address);
      detail_row(dx, r.y + 88, dw, tr("Connection"), config.away ? tr("Away") : tr("At home"));
      detail_row(dx, r.y + 124, dw, tr("Wake-on-LAN"),
                 config.away ? tr("At home only") : d->mac[0] ? tr("Ready") : tr("Not ready yet"));
      detail_row(dx, r.y + 160, dw, tr("Stream"), stream);
    } else {
      draw_icon(ICON_DESKTOP, r.x + 18, r.y + r.h / 2, TH_TEXT2);
      theme_text_fit(r.x + 52, r.y + 26, TH_TEXT, TS_BODY, d->name, 300);
      state_line(r.x + 52, r.y + 47, state, paused);
      theme_text_fit_right(r.x + r.w - 18, r.y + 26, TH_TEXT2, TS_SMALL, address, 260);
      theme_text_fit_right(r.x + r.w - 18, r.y + 46, TH_TEXT3, TS_MICRO,
                           config.away ? tr("Away connection")
                                       : d->mac[0] ? tr("Wake-on-LAN ready") : tr("Wake-on-LAN not ready"), 260);
    }
  }
  if (view.connected)
    return;
  if (n == 0)
    draw_wrapped(AREA_X, PREVIEW_TOP + 24, TS_HINT, TH_TEXT2, tr("No PC yet: search for yours on the Wi-Fi or type its address."),
                 (int)AREA_W, 2, 24);
  draw_choice(panel_rect[n], ICON_SEARCH, tr("Search the network"), selected(HOME_MENU_COMPUTERS, n));
  draw_choice(panel_rect[n + 1], ICON_KEYBOARD, tr("Enter an address"), selected(HOME_MENU_COMPUTERS, n + 1));
}

/* A card of the Settings or Help preview: a small title, a value in large
 * type and up to three lines under it. */
static void draw_card(rect r, const char *title, const home_card *c, bool focused) {
  card_box(r.x, r.y, r.w, r.h, TILE_R, focused);
  bool narrow = r.w < 200;   /* the four columns of the Settings preview */
  float pad = narrow ? 14 : 16;
  int w = (int)(r.w - 2 * pad);
  int small = narrow ? TS_MICRO : TS_SMALL;
  float step = narrow ? 17 : 19;
  theme_text_fit(r.x + pad, r.y + 30, focused ? TH_TEXT : TH_TEXT3, TS_SMALL, title, w);
  int size = theme_text_width(TS_LEAD, c->value) <= w ? TS_LEAD : TS_BODY;
  theme_text_fit(r.x + pad, r.y + 68, c->warn ? TH_WARN : TH_TEXT, size, c->value, w);
  float y = r.y + 96;
  int left = 3;
  for (int k = 0; k < 3 && left > 0; k++) {
    if (!c->line[k][0])
      continue;
    char lines[3][256];
    int n = theme_wrap(small, c->line[k], w, lines, left);
#ifdef UI_PREVIEW
    char rest[3][256];
    if (theme_wrap(small, c->line[k], w, rest, 3) > n)
      fprintf(stderr, "CARD TEXT CUT: %s\n", c->line[k]);
#endif
    for (int i = 0; i < n; i++, y += step)
      theme_text(r.x + pad, y, TH_TEXT3, small, lines[i]);
    left -= n;
  }
}

static void draw_settings_preview(void) {
  preview_title(tr("Settings"), NULL);
  for (int c = 0; c < SETTINGS_CAT_COUNT && c < panel.count; c++)
    draw_card(panel_rect[c], ui_settings_category(c), &view.settings[c], selected(HOME_MENU_SETTINGS, c));
}

static void draw_help_preview(void) {
  preview_title(tr("Help"), NULL);
  for (int t = 0; t < HELP_COUNT && t < panel.count; t++)
    draw_card(panel_rect[t], ui_help_title(t), &view.help[t], selected(HOME_MENU_HELP, t));
}

static void draw_quit_preview(void) {
  preview_title(tr("Quit"), NULL);
  draw_wrapped(AREA_X, PREVIEW_TOP + 24, TS_HINT, TH_TEXT2, tr("Closes VitaMoon and returns to the Vita home screen."),
               (int)AREA_W, 2, 24);
}

/* ---------------- hints ---------------- */

static const char *hero_action(void) {
  switch (hero_now()) {
    case HERO_ADD: return tr("Add a computer");
    case HERO_PAUSED: return tr("Resume");
    case HERO_WAKE: return tr("Wake the PC");
    case HERO_OPTIONS: return tr("PC options");
    case HERO_HOME: return tr("Switch to At home");
    case HERO_AWAY: return tr("Switch to Away");
    case HERO_CONTINUE: return tr("Continue");
    default: return tr("Open the library");
  }
}

/* The PC that square acts on: the one selected in the Computers preview, else
 * the one the dashboard shows; -1: none (or a stream is paused). */
static int square_host(void) {
  if (view.connected)
    return -1;
  if (nav.in_panel && nav.menu == HOME_MENU_COMPUTERS && nav.item < pcs_shown())
    return view.paired[nav.item];
  return view.host;
}

static int home_hints(theme_hint_item *out) {
  static char open_label[96];
  int n = 0;
  if (nav.in_panel) {
    const char *what = tr("Open");
    switch (nav.menu) {
      case HOME_MENU_HOME:
        what = nav.item == 0 ? hero_action() : nav.item <= view.row_count ? tr("Play") : tr("Open the library");
        break;
      case HOME_MENU_LIBRARY: what = tr("Play"); break;
      case HOME_MENU_COMPUTERS:
        if (nav.item < pcs_shown()) {
          int i = view.connected ? view.host : view.paired[nav.item];
          int state = view.connected ? HOST_STATE_ONLINE : ui_host_state(i);
          what = view.connected ? tr("Resume")
                 : state == HOST_STATE_ONLINE || state == HOST_STATE_AT_HOME || state == HOST_STATE_AWAY_ONLY
                     ? tr("Connect")
                     : tr("Open");
        }
        break;
      case HOME_MENU_SETTINGS:
        snprintf(open_label, sizeof(open_label), tr("Open %s"), ui_settings_category(nav.item));
        what = open_label;
        break;
      default: break;
    }
    out[n++] = (theme_hint_item){theme_confirm_glyph(), what};
    out[n++] = (theme_hint_item){theme_cancel_glyph(), tr("Menu")};
    if (square_host() >= 0 && (nav.menu == HOME_MENU_HOME || nav.menu == HOME_MENU_COMPUTERS))
      out[n++] = (theme_hint_item){GLYPH_SQUARE, tr("PC options")};
  } else {
    const char *what = nav.menu == HOME_MENU_HOME ? tr("Select") : nav.menu == HOME_MENU_QUIT ? tr("Quit") : tr("Open");
    out[n++] = (theme_hint_item){theme_confirm_glyph(), what};
    if (nav.menu != HOME_MENU_HOME && panel.count > 0)
      out[n++] = (theme_hint_item){GLYPH_DPAD_RIGHT, tr("Browse")};
    if (nav.menu != HOME_MENU_HOME)
      out[n++] = (theme_hint_item){theme_cancel_glyph(), tr("Home")};
    else if (!view.connected)
      out[n++] = (theme_hint_item){GLYPH_TRIANGLE, tr("Refresh")};
  }
  out[n++] = (theme_hint_item){GLYPH_START, tr("Settings")};
  return n;
}

/* ---------------- actions ---------------- */

/* Leaves the home screen for another one, then comes back to it. */
static void before_page(bool stop_scan) {
  covers_free();
  ui_end();
  if (stop_scan)
    stop_host_scan_thread();
}

static void after_page(void) {
  load_view();
  layout_now();
  home_nav_fix(&nav, &panel);
  scan_start();
  gui_touch_reset();
  PREVIEW_SCREEN("home");
}

/* The PC shown, opened: its library, or straight into a game when app_id is
 * not 0. A PC the scan saw off (or at a new address) gets its own menu
 * instead: wake it, connect anyway, update the address. */
static void open_host(int app_id) {
  if (view.connected) {
    before_page(false);
    if (app_id)
      ui_connect_resume_launch();
    else
      ui_connect_resume();
    after_page();
    return;
  }
  device_info_t *host = view.host >= 0 ? &known_devices.devices[view.host] : NULL;
  if (!host) {
    before_page(false);
    ui_computers_page();
    after_page();
    return;
  }
  int state = shown_state();
  before_page(true);
  if (state >= 0)
    ui_host_open(view.host, app_id);
  /* not seen by the scan yet: try it at once */
  else if (app_id)
    ui_connect_paired_device_launch(host, app_id);
  else
    ui_connect_paired_device(host);
  after_page();
}

static void open_host_menu(int host) {
  before_page(true);
  ui_host_manage_menu(&known_devices.devices[host]);
  after_page();
}

static void act_hero(void) {
  hero_kind kind = hero_now();
  switch (kind) {
    case HERO_ADD:
      before_page(false);
      ui_computers_page();
      after_page();
      break;
    case HERO_PAUSED: open_host(view.paused_id); break;
    case HERO_WAKE:
      LOGI("home", "wake %s from the large card", known_devices.devices[view.host].name);
      ui_end();
      ui_host_wake(&known_devices.devices[view.host]);
      gui_touch_reset();
      break;
    case HERO_OPTIONS: open_host_menu(view.host); break;
    case HERO_HOME:
    case HERO_AWAY:
      /* the other address; the scan looks again */
      LOGI("home", "connection switched to %s from the large card", kind == HERO_AWAY ? "Away" : "Home");
      before_page(true);
      config.away = kind == HERO_AWAY;
      ui_settings_save_config();
      after_page();
      break;
    default: open_host(view.resume ? view.resume->id : 0); break;
  }
}

static void act_computers(int item) {
  int n = pcs_shown();
  if (view.connected) {
    open_host(0);   /* the paused stream's PC: back to it */
    return;
  }
  if (item < n) {
    int i = view.paired[item];
    before_page(true);
    ui_host_open(i, 0);
    after_page();
  } else {
    before_page(true);
    if (item == n)
      ui_search_device();
    else
      ui_connect_manual();
    after_page();
  }
}

static void open_settings(int category) {
  before_page(true);
  if (category < 0)
    ui_settings_menu();
  else
    ui_settings_menu_at(category);
  after_page();
}

static void open_help(int topic) {
  before_page(false);
  ui_help(topic);
  after_page();
}

static void act_item(void) {
  switch (nav.menu) {
    case HOME_MENU_HOME:
      if (nav.item == 0)
        act_hero();
      else if (nav.item <= view.row_count)
        open_host(view.row[nav.item - 1].game->id);
      else
        open_host(0);
      break;
    case HOME_MENU_LIBRARY:
      if (nav.item < view.games.shelf_count)
        open_host(view.games.shelf[nav.item].id);
      break;
    case HOME_MENU_COMPUTERS: act_computers(nav.item); break;
    case HOME_MENU_SETTINGS: open_settings(nav.item); break;
    case HOME_MENU_HELP: open_help(nav.item); break;
    default: break;
  }
}

static void act_open(void) {
  switch (nav.menu) {
    case HOME_MENU_LIBRARY: open_host(0); break;
    case HOME_MENU_COMPUTERS:
      before_page(false);
      ui_computers_page();
      after_page();
      break;
    case HOME_MENU_SETTINGS: open_settings(-1); break;
    case HOME_MENU_HELP: open_help(HELP_STREAM); break;
    case HOME_MENU_QUIT:
      LOGI("app", "quit from the home screen");
      before_page(true);
      if (connection_get_status() != LI_DISCONNECTED)
        connection_terminate();
      exit(0);
    default: break;
  }
}

/* ---------------- touch ---------------- */

/* A tap selects what it touches; a tap on what is selected acts on it. */
static home_action touch_action(int tx, int ty) {
  if (tx < DIVIDER_X) {
    for (int m = 0; m < HOME_MENU_COUNT; m++) {
      if (!inside(tx, ty, (rect){MENU_X, menu_row_y(m), MENU_W, MENU_ROW_H}))
        continue;
      bool focused = !nav.in_panel && nav.menu == m;
      nav.in_panel = false;
      nav.menu = m;
      layout_now();
      return focused ? home_nav_key(&nav, HOME_KEY_CONFIRM, &panel) : HOME_ACT_NONE;
    }
    return HOME_ACT_NONE;
  }
  for (int i = 0; i < panel.count; i++) {
    if (!inside(tx, ty, panel_rect[i]))
      continue;
    bool focused = nav.in_panel && nav.item == i;
    nav.in_panel = true;
    nav.item = i;
    return focused ? HOME_ACT_ITEM : HOME_ACT_NONE;
  }
  return HOME_ACT_NONE;
}

/* ---------------- the screen ---------------- */

void ui_home(void) {
#ifdef BUILD_ID
  snprintf(version_text, sizeof(version_text), "%s  %s", VERSION_TEXT, BUILD_ID);
#else
  snprintf(version_text, sizeof(version_text), "%s", VERSION_TEXT);
#endif
  load_view();
  nav = (home_nav){true, HOME_MENU_HOME, 0};
  layout_now();
  home_nav_fix(&nav, &panel);
  scan_start();
  gui_touch_reset();
  PREVIEW_SCREEN("home");

  /* after the start-up screen, everything below the top bar fades in */
  float appear = splash_take_intro() ? 0.0f : 1.0f;

  while (true) {
    scan_check();
    ui_hosts_refresh_internet_address();
    if (g_host_status_changed && !view.connected)
      g_host_status_changed = 0;   /* seen: the states are read every frame */

    /* ---- input ---- */
    layout_now();
    int buttons = read_buttons();
    bool hold = (buttons & SCE_CTRL_HOLD) != 0;
    home_action act = HOME_ACT_NONE;
    /* arrows repeat while held (as in the other menus); the other buttons act once */
    static const struct { int button; home_key key; } k_arrows[] = {
      {SCE_CTRL_UP, HOME_KEY_UP}, {SCE_CTRL_DOWN, HOME_KEY_DOWN},
      {SCE_CTRL_LEFT, HOME_KEY_LEFT}, {SCE_CTRL_RIGHT, HOME_KEY_RIGHT},
    };
    for (int k = 0; k < 4; k++) {
      if (!(buttons & k_arrows[k].button))
        continue;
      act = home_nav_key(&nav, k_arrows[k].key, &panel);
      layout_now();   /* the menu entry may have changed */
    }
    if (!hold && (buttons & config.btn_confirm))
      act = home_nav_key(&nav, HOME_KEY_CONFIRM, &panel);
    else if (!hold && (buttons & config.btn_cancel))
      act = home_nav_key(&nav, HOME_KEY_BACK, &panel);

    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    int tx, ty;
    if (act == HOME_ACT_NONE && gui_touch_tap(&touch, &tx, &ty)) {
      if (ty < 56 && tx < 200 && connection_get_status() == LI_MINIMIZED) {
        /* the top-left corner resumes a paused stream, as on the other screens */
        nav = (home_nav){true, HOME_MENU_HOME, 0};
        layout_now();
        act = HOME_ACT_ITEM;
      } else {
        act = touch_action(tx, ty);
      }
    }

    if (act == HOME_ACT_NONE && !hold) {
      if (buttons & SCE_CTRL_START) {
        open_settings(-1);
        continue;
      }
      if ((buttons & SCE_CTRL_TRIANGLE) && !view.connected) {
        LOGI("home", "refresh: the host scan starts again");
        stop_host_scan_thread();
        scan_start();
      }
      int host = square_host();
      if ((buttons & SCE_CTRL_SQUARE) && host >= 0) {
        open_host_menu(host);
        continue;
      }
    }

    if (act == HOME_ACT_OPEN) {
      act_open();
      continue;
    }
    if (act == HOME_ACT_ITEM) {
      act_item();
      continue;
    }

    /* ---- covers: one per frame ---- */
    covers_load_one();

    /* ---- draw ---- */
    int key = nav.in_panel ? nav.menu * HOME_PANEL_MAX + nav.item : -1;
    if (key != lift_key) {
      lift_key = key;
      lift = 0.35f;
    }
    lift = theme_approach(lift, 1.0f);
    appear = appear + 0.08f > 1.0f ? 1.0f : appear + 0.08f;
    ui_start();
    theme_topbar(NULL);
    theme_set_alpha(appear);
    draw_menu();
    switch (nav.menu) {
      case HOME_MENU_HOME: draw_dashboard(); break;
      case HOME_MENU_LIBRARY: draw_library_preview(); break;
      case HOME_MENU_COMPUTERS: draw_computers_preview(); break;
      case HOME_MENU_SETTINGS: draw_settings_preview(); break;
      case HOME_MENU_HELP: draw_help_preview(); break;
      default: draw_quit_preview(); break;
    }
    theme_text(MENU_X, TH_HINTS_Y, TH_TEXT3, TS_MICRO, version_text);
    theme_hint_item hints[6];
    theme_hints(hints, home_hints(hints));
    theme_set_alpha(1.0f);
    ui_end();
  }
}
