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
 * under the cursor. On Home it is a dashboard of tiles in a 3 x 3 grid: the
 * game to pick up (2 x 2), the PC, the last session, and a strip of games.
 *
 * Kept light: the PC files are read when the screen opens or comes back, not
 * while it is shown; covers are loaded one per frame, at most 16, and freed
 * before any other screen (a stream needs the memory).
 */
#include "ui_home.h"
#include "home_logic.h"
#include "home_data.h"
#include "cover_look.h"
#include "library_logic.h"
#include "boxart.h"
#include "guilib.h"
#include "theme.h"
#include "splash.h"
#include "preview_hooks.h"
#include "ui_connect.h"
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

void ui_host_manage_menu(device_info_t *info);

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
#define COL_W ((AREA_W - 2 * GAP) / 3)       /* 214.7 */
#define ROW_H ((AREA_H - 2 * GAP) / 3)       /* 122.7 */
#define TILE_R 16.0f
#define PREVIEW_TOP 128.0f                  /* under a preview's title */
#define ICON_SIZE ICON_HOME_SIZE

/* ---- what the screen shows, read when it opens or comes back ---- */
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
  const home_game *strip[3];
  int strip_count;
  bool strip_recent;              /* the strip starts with recent games */
  int paused_id;                  /* the paused stream's game */
  char paused_name[128];
} home_view;

static home_view view;
static home_nav nav = {true, HOME_MENU_HOME, HOME_TILE_RESUME};
static char version_text[96];

/* Host scan: the PCs' states. A PC not seen yet shows "Checking..." for the
 * few seconds a first pass takes, rather than "Offline". */
static SceUInt64 scan_since = 0;
static bool scan_settled = false;

/* ---------------- covers ---------------- */

#define COVER_SLOTS 16
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
    view.strip_count = home_strip_games(&view.games, skip, view.strip, 3);
    view.strip_recent = view.strip_count > 0 && view.games.recent_count > 0 &&
                        view.strip[0] >= view.games.recent && view.strip[0] < view.games.recent + HOME_RECENT_MAX;
  }
}

static unsigned tiles_on(void) {
  if (view.host < 0 && !view.connected)
    return HOME_TILE_BIT(HOME_TILE_RESUME);   /* "Add a computer" */
  unsigned t = HOME_TILE_BIT(HOME_TILE_RESUME) | HOME_TILE_BIT(HOME_TILE_ALL);
  if (view.host >= 0)
    t |= HOME_TILE_BIT(HOME_TILE_PC);
  for (int k = 0; k < view.strip_count; k++)
    t |= HOME_TILE_BIT(HOME_TILE_GAME0 + k);
  return t;
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

/* "Today at 19:46", "3 days ago"... from a "YYYY-MM-DD HH:MM" moment. */
static void describe_when(const char *when, char *out, size_t n) {
  lib_moment then, today;
  SceDateTime now;
  out[0] = 0;
  if (!lib_parse_moment(when, &then) || sceRtcGetCurrentClockLocalTime(&now) < 0) {
    snprintf(out, n, "%s", when ? when : "");
    return;
  }
  today = (lib_moment){now.year, now.month, now.day, now.hour, now.minute};
  int days = 0;
  switch (lib_when(&then, &today, &days)) {
    case LIB_WHEN_TODAY: snprintf(out, n, tr("today at %02d:%02d"), then.hour, then.minute); break;
    case LIB_WHEN_YESTERDAY: snprintf(out, n, tr("yesterday at %02d:%02d"), then.hour, then.minute); break;
    case LIB_WHEN_DAYS_AGO: snprintf(out, n, tr("%d days ago"), days); break;
    default: snprintf(out, n, tr("on %02d/%02d/%04d"), then.day, then.month, then.year); break;
  }
  if (out[0] >= 'a' && out[0] <= 'z')
    out[0] = (char)(out[0] - 'a' + 'A');   /* it starts a line here */
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

static void tile_box(float x, float y, float w, float h, bool focused) {
  theme_rounded_rect(x, y, w, h, TILE_R, TH_PANEL);
  theme_rounded_outline(x, y, w, h, TILE_R, focused ? TH_ACCENT : TH_PANEL_EDGE);
  if (focused)
    theme_rounded_outline(x - 3, y - 3, w + 6, h + 6, TILE_R + 3, theme_alpha(TH_ACCENT, 110));
}

/* A game's cover, or its coloured tile with the initials until it is downloaded. */
static void draw_cover(const home_game *g, float x, float y, float w, float h, int initials_size) {
  theme_rounded_rect(x + 2, y + 5, w, h, 10, RGBA8(0, 0, 0, 90));
  vita2d_texture *tex = cover_of(g->id);
  if (tex) {
    vita2d_draw_texture_tint_scale(tex, x, y, w / BOXART_W, h / BOXART_H, theme_color(RGBA8(255, 255, 255, 255)));
    return;
  }
  theme_rounded_rect(x, y, w, h, 10, cover_tint(g->id));
  char initials[4];
  cover_initials(g->name, initials);
  theme_text(x + 10, y + initials_size + 8, RGBA8(255, 255, 255, 225), initials_size, initials);
  if (w >= 90) {
    theme_rounded_rect(x + 5, y + h - 27, w - 10, 22, 7, theme_alpha(TH_INK, 120));
    theme_text_fit(x + 11, y + h - 11, TH_TEXT, TS_MICRO, g->name, (int)w - 22);
  }
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
    dot = state == HOST_STATE_ONLINE ? TH_OK : state == HOST_STATE_MOVED ? TH_WARN : TH_ERR;
    text = ui_host_state_text(state);
  }
  theme_circle(x + 4, baseline - 4, 4, dot);
  theme_text(x + 14, baseline, TH_TEXT3, TS_MICRO, text);
}

/* ---------------- the side menu ---------------- */

static const char *const k_menu_icons[HOME_MENU_COUNT] = {ICON_HOME, ICON_GRID, ICON_DESKTOP, ICON_COG, ICON_POWER};

static const char *menu_label(int m) {
  switch (m) {
    case HOME_MENU_HOME: return tr("Home");
    case HOME_MENU_LIBRARY: return tr("Library");
    case HOME_MENU_COMPUTERS: return tr("Computers");
    case HOME_MENU_SETTINGS: return tr("Settings");
    default: return tr("Quit");
  }
}

static float menu_row_y(int m) {
  return m == HOME_MENU_QUIT ? MENU_QUIT_Y : MENU_Y + m * MENU_ROW_STEP;
}

static void draw_menu(void) {
  for (int m = 0; m < HOME_MENU_COUNT; m++) {
    float y = menu_row_y(m);
    bool focus = !nav.in_tiles && nav.menu == m;
    bool current = nav.in_tiles && m == HOME_MENU_HOME;
    if (focus) {
      theme_rounded_rect(MENU_X, y, MENU_W, MENU_ROW_H, 12, theme_alpha(TH_ACCENT, 38));
      theme_rounded_outline(MENU_X, y, MENU_W, MENU_ROW_H, 12, TH_ACCENT);
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

/* ---------------- Home: the dashboard ---------------- */

static float col_x(int c) { return AREA_X + c * (COL_W + GAP); }
static float row_y(int r) { return AREA_Y + r * (ROW_H + GAP); }

/* What confirming the large tile does, as its button says it. */
static const char *resume_button(void) {
  if (view.connected)
    return tr("Resume");
  if (view.host < 0)
    return tr("Add a computer");
  int state = shown_state();
  if (state == HOST_STATE_OFFLINE || state == HOST_STATE_MOVED)
    return tr("PC options");
  return view.resume ? tr("Continue") : tr("Open the library");
}

static void draw_resume_tile(bool focused) {
  float x = col_x(0), y = row_y(0), w = 2 * COL_W + GAP, h = 2 * ROW_H + GAP;
  const device_info_t *host = shown_host();

  if (view.host < 0 && !view.connected) {
    /* no PC yet: one card over the whole area */
    w = AREA_W;
    h = AREA_H;
    tile_box(x, y, w, h, focused);
    theme_text(x + 32, y + 64, TH_ACCENT, TS_SMALL, tr("Welcome"));
    theme_text_fit(x + 32, y + 104, TH_TEXT, TS_TITLE, tr("Add a computer"), (int)w - 64);
    draw_wrapped(x + 32, y + 146, TS_HINT, TH_TEXT2,
                 tr("VitaMoon plays the games of a PC that runs Sunshine. Add yours: search for it on the Wi-Fi or type its address."),
                 (int)w - 64, 3, 24);
    draw_button(x + 32, y + h - 32 - 46, 260, 46, ICON_DESKTOP, resume_button(), true);
    return;
  }

  tile_box(x, y, w, h, focused);
  float cover_x = x + 16, cover_y = y + (h - 200) / 2;
  float tx = cover_x + 150 + 18, tw = x + w - 16 - tx;
  const char *label, *title;
  char line1[160] = "", line2[160] = "";
  home_game paused_game;
  const home_game *game = view.resume;

  if (view.connected) {
    memset(&paused_game, 0, sizeof(paused_game));
    paused_game.id = view.paused_id;
    snprintf(paused_game.name, sizeof(paused_game.name), "%.95s", view.paused_name);
    game = &paused_game;
    label = tr("Paused");
    title = view.paused_name[0] ? view.paused_name : ui_connect_host_label();
    snprintf(line1, sizeof(line1), tr("on %s"), ui_connect_host_label());
    snprintf(line2, sizeof(line2), "%s", tr("Still running on the PC"));
  } else if (game) {
    label = tr("Continue");
    title = game->name;
    snprintf(line1, sizeof(line1), tr("on %s"), host ? host->name : "");
    char when[64], length[32];
    describe_when(view.has_last ? view.last.when : game->when, when, sizeof(when));
    if (view.has_last) {
      describe_length(view.last.seconds, length, sizeof(length));
      snprintf(line2, sizeof(line2), "%s · %s", when, length);
    } else {
      snprintf(line2, sizeof(line2), "%s", when);
    }
  } else {
    label = tr("Library");
    title = host ? host->name : "";
    snprintf(line1, sizeof(line1), "%s", tr("Open the library once: your games will show up here."));
  }

  if (game && (game->id || game->name[0])) {
    draw_cover(game, cover_x, cover_y, 150, 200, TS_TITLE);
  } else {
    theme_rounded_rect(cover_x, cover_y, 150, 200, 10, TH_FILL_SOFT);
    theme_rounded_outline(cover_x, cover_y, 150, 200, 10, TH_DIVIDER);
    draw_icon_centered(ICON_GRID, cover_x + 75, cover_y + 100, TH_TEXT3);
  }

  theme_text_fit(tx, y + 40, TH_ACCENT, TS_MICRO, label, (int)tw);
  float ty = draw_wrapped(tx, y + 72, TS_LEAD, TH_TEXT, title, (int)tw, 2, 27);
  ty = draw_wrapped(tx, ty + 2, TS_SMALL, TH_TEXT3, line1, (int)tw, game ? 1 : 3, 20);
  if (line2[0])
    theme_text_fit(tx, ty, TH_TEXT3, TS_SMALL, line2, (int)tw);
  bool play = view.connected || (game && shown_state() != HOST_STATE_OFFLINE && shown_state() != HOST_STATE_MOVED);
  draw_button(tx, cover_y + 200 - 44, tw, 44, play ? ICON_PLAY : NULL, resume_button(), true);
}

static void draw_pc_tile(bool focused) {
  float x = col_x(2), y = row_y(0), w = COL_W, h = ROW_H;
  const device_info_t *host = shown_host();
  tile_box(x, y, w, h, focused);
  if (!host)
    return;
  state_line(x + 16, y + 30, shown_state(), view.connected ? tr("Stream paused") : NULL);
  theme_text_fit(x + 16, y + 58, TH_TEXT, TS_BODY, host->name, (int)w - 32);
  char line[320];
  snprintf(line, sizeof(line), "%s · %s", host->internal, host->paired ? tr("paired") : tr("not paired"));
  theme_text_fit(x + 16, y + 82, TH_TEXT3, TS_MICRO, line, (int)w - 32);
  theme_text_fit(x + 16, y + 100, TH_TEXT3, TS_MICRO,
                 host->mac[0] ? tr("Wake-on-LAN ready") : tr("Wake-on-LAN not ready"), (int)w - 32);
}

static void draw_session_tile(void) {
  float x = col_x(2), y = row_y(1), w = COL_W, h = ROW_H;
  tile_box(x, y, w, h, false);
  char value[64], line[160];
  if (view.has_last && config.library_stats) {
    theme_text_fit(x + 16, y + 28, TH_TEXT3, TS_MICRO, tr("Last session"), (int)w - 32);
    snprintf(value, sizeof(value), tr("%d.%d"), view.last.shown_fps_x10 / 10, view.last.shown_fps_x10 % 10);
    theme_text(x + 16, y + 60, view.last.shown_fps_x10 >= 575 ? TH_OK : TH_TEXT, TS_LEAD, value);
    int vw = theme_text_width(TS_LEAD, value);
    theme_text_fit(x + 16 + vw, y + 60, TH_TEXT2, TS_SMALL, tr(" fps shown"), (int)(w - 32 - vw));
    snprintf(line, sizeof(line), "%d ms%s", (view.last.r2p_ms_x10 + 5) / 10, tr(" receive to screen"));
    theme_text_fit(x + 16, y + 84, TH_TEXT3, TS_MICRO, line, (int)w - 32);
    snprintf(line, sizeof(line), "%u%s", view.last.lost,
             i18n_plural(view.last.lost) ? tr(" frames lost") : tr(" frame lost"));
    theme_text_fit(x + 16, y + 102, view.last.lost ? TH_WARN : TH_TEXT3, TS_MICRO, line, (int)w - 32);
    return;
  }
  /* no session to show: what the next stream will be */
  theme_text_fit(x + 16, y + 28, TH_TEXT3, TS_MICRO, tr("Stream"), (int)w - 32);
  ui_settings_value(SETTINGS_SUM_RESOLUTION, value, sizeof(value));
  theme_text_fit(x + 16, y + 60, TH_TEXT, TS_LEAD, value, (int)w - 32);
  char fps[32], rate[48];
  ui_settings_value(SETTINGS_SUM_FPS, fps, sizeof(fps));
  ui_settings_value(SETTINGS_SUM_BITRATE, rate, sizeof(rate));
  theme_text_fit(x + 16, y + 84, TH_TEXT3, TS_MICRO, fps, (int)w - 32);
  theme_text_fit(x + 16, y + 102, TH_TEXT3, TS_MICRO, rate, (int)w - 32);
}

/* The strip's slots: three games and "See all". */
#define STRIP_ALL_W 92.0f
static void strip_slot(int k, float *x, float *w) {
  float left = AREA_X + 16, inner = AREA_W - 32 - STRIP_ALL_W - 3 * GAP;
  float game_w = inner / 3;
  if (k < 3) {
    *x = left + k * (game_w + GAP);
    *w = game_w;
  } else {
    *x = AREA_X + AREA_W - 16 - STRIP_ALL_W;
    *w = STRIP_ALL_W;
  }
}

static void draw_strip(int focused_tile) {
  float x = col_x(0), y = row_y(2), w = AREA_W, h = ROW_H;
  tile_box(x, y, w, h, false);
  const device_info_t *host = shown_host();
  char label[200];
  snprintf(label, sizeof(label), view.strip_recent ? tr("Recent games · %s") : tr("Games · %s"),
           host ? host->name : ui_connect_host_label());
  theme_text_fit(x + 16, y + 24, TH_TEXT3, TS_MICRO, label, (int)w - 32);

  float slot_y = y + 36;
  if (view.strip_count == 0) {
    float sx, sw;
    strip_slot(0, &sx, &sw);
    draw_wrapped(sx, slot_y + 28, TS_SMALL, TH_TEXT2, tr("Open the library once: your games will show up here."),
                 (int)(3 * sw + 2 * GAP), 2, 20);
  }
  for (int k = 0; k < view.strip_count; k++) {
    float sx, sw;
    strip_slot(k, &sx, &sw);
    const home_game *g = view.strip[k];
    if (focused_tile == HOME_TILE_GAME0 + k) {
      theme_rounded_rect(sx - 6, slot_y - 5, sw + 6, 82, 12, TH_FILL_FOCUS);
      theme_rounded_outline(sx - 6, slot_y - 5, sw + 6, 82, 12, TH_ACCENT);
    }
    draw_cover(g, sx, slot_y, 54, 72, TS_HINT);
    float tx = sx + 64, tw = sw - 64;
    float ty = draw_wrapped(tx, slot_y + 22, TS_SMALL, TH_TEXT, g->name, (int)tw, 2, 18);
    if (g->when[0]) {
      char when[64];
      describe_when(g->when, when, sizeof(when));
      theme_text_fit(tx, ty + 2, TH_TEXT3, TS_MICRO, when, (int)tw);
    }
  }
  float ax, aw;
  strip_slot(3, &ax, &aw);
  bool all = focused_tile == HOME_TILE_ALL;
  theme_rounded_rect(ax, slot_y, aw, 72, 12, all ? TH_FILL_FOCUS : TH_FILL_SOFT);
  theme_rounded_outline(ax, slot_y, aw, 72, 12, all ? TH_ACCENT : TH_DIVIDER);
  draw_icon_centered(ICON_GRID, ax + aw / 2, slot_y + 26, all ? TH_TEXT : TH_TEXT2);
  theme_text_fit_center(ax + aw / 2, slot_y + 58, all ? TH_TEXT : TH_TEXT2, TS_MICRO, tr("See all"), (int)aw - 8);
}

static void draw_dashboard(void) {
  int focus = nav.in_tiles ? nav.tile : -1;
  draw_resume_tile(focus == HOME_TILE_RESUME);
  if (view.host < 0 && !view.connected)
    return;
  draw_pc_tile(focus == HOME_TILE_PC);
  draw_session_tile();
  draw_strip(focus);
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
  const float cw = 100, ch = 133, gap = (AREA_W - 6 * cw) / 5;
  for (int i = 0; i < view.games.shelf_count && i < HOME_SHELF_MAX; i++) {
    float x = AREA_X + (i % 6) * (cw + gap), y = PREVIEW_TOP + (i / 6) * (ch + 16);
    draw_cover(&view.games.shelf[i], x, y, cw, ch, TS_LEAD);
  }
}

static void draw_computers_preview(void) {
  char sub[64];
  snprintf(sub, sizeof(sub), i18n_plural(view.paired_count) ? tr("%d paired PCs") : tr("%d paired PC"),
           view.paired_count);
  preview_title(tr("Computers"), sub);
  float y = PREVIEW_TOP;
  int shown = view.paired_count > 4 ? 4 : view.paired_count;
  for (int k = 0; k < shown; k++) {
    int i = view.paired[k];
    const device_info_t *d = &known_devices.devices[i];
    theme_rounded_rect(AREA_X, y, AREA_W, 60, 14, TH_PANEL);
    theme_rounded_outline(AREA_X, y, AREA_W, 60, 14, TH_PANEL_EDGE);
    draw_icon(ICON_DESKTOP, AREA_X + 18, y + 30, TH_TEXT2);
    theme_text_fit(AREA_X + 52, y + 26, TH_TEXT, TS_BODY, d->name, 300);
    if (view.connected) {
      if (i == view.host)
        state_line(AREA_X + 52, y + 47, 0, tr("Stream paused"));
    } else {
      int s = ui_host_state(i);
      state_line(AREA_X + 52, y + 47, s == HOST_STATE_OFFLINE && !scan_settled ? -1 : s, NULL);
    }
    char line[320];
    snprintf(line, sizeof(line), "%s : %d", d->internal, d->port);
    theme_text_fit_right(AREA_X + AREA_W - 18, y + 26, TH_TEXT2, TS_SMALL, line, 260);
    theme_text_fit_right(AREA_X + AREA_W - 18, y + 46, TH_TEXT3, TS_MICRO,
                         d->mac[0] ? tr("Wake-on-LAN ready") : tr("Wake-on-LAN not ready"), 260);
    y += 70;
  }
  if (view.paired_count == 0) {
    draw_wrapped(AREA_X, y + 24, TS_HINT, TH_TEXT2, tr("No PC yet: search for yours on the Wi-Fi or type its address."),
                 (int)AREA_W, 2, 24);
    y += 70;
  }
  float bw = (AREA_W - GAP) / 2;
  draw_button(AREA_X, y + 6, bw, 50, ICON_SEARCH, tr("Search the network"), false);
  draw_button(AREA_X + bw + GAP, y + 6, bw, 50, ICON_KEYBOARD, tr("Enter an address"), false);
}

static void settings_card(int col, int row, const char *category, const char *value, const char *line1,
                          const char *line2) {
  float w = COL_W, h = (TH_CONTENT_END - PREVIEW_TOP - GAP) / 2;
  float x = col_x(col), y = PREVIEW_TOP + row * (h + GAP);
  theme_rounded_rect(x, y, w, h, TILE_R, TH_PANEL);
  theme_rounded_outline(x, y, w, h, TILE_R, TH_PANEL_EDGE);
  theme_text_fit(x + 16, y + 28, TH_TEXT3, TS_MICRO, category, (int)w - 32);
  /* the value in large type when it fits, else smaller */
  int size = theme_text_width(TS_LEAD, value) <= w - 32 ? TS_LEAD : TS_BODY;
  theme_text_fit(x + 16, y + 70, TH_TEXT, size, value, (int)w - 32);
  if (line1 && line1[0])
    theme_text_fit(x + 16, y + 96, TH_TEXT3, TS_MICRO, line1, (int)w - 32);
  if (line2 && line2[0])
    theme_text_fit(x + 16, y + 114, TH_TEXT3, TS_MICRO, line2, (int)w - 32);
}

static void draw_settings_preview(void) {
  preview_title(tr("Settings"), tr("what is set now"));
  char v[64], a[64], b[96], c[96];

  ui_settings_value(SETTINGS_SUM_RESOLUTION, v, sizeof(v));
  ui_settings_value(SETTINGS_SUM_FPS, a, sizeof(a));
  ui_settings_value(SETTINGS_SUM_BITRATE, c, sizeof(c));
  snprintf(b, sizeof(b), tr("%s: %s"), ui_settings_label(SETTINGS_SUM_BITRATE), c);
  settings_card(0, 0, tr("Video"), v, a, b);

  ui_settings_value(SETTINGS_SUM_PROFILE, v, sizeof(v));
  ui_settings_value(SETTINGS_SUM_LL_AUDIO, a, sizeof(a));
  snprintf(b, sizeof(b), tr("%s: %s"), ui_settings_label(SETTINGS_SUM_LL_AUDIO), a);
  settings_card(1, 0, tr("Performance"), v, ui_settings_label(SETTINGS_SUM_PROFILE), b);

  ui_settings_value(SETTINGS_SUM_CONTROLLER, v, sizeof(v));
  settings_card(2, 0, tr("Controls"), v, ui_settings_label(SETTINGS_SUM_CONTROLLER), NULL);

  ui_settings_value(SETTINGS_SUM_TOUCH, v, sizeof(v));
  ui_settings_value(SETTINGS_SUM_REAR_TOUCH, a, sizeof(a));
  snprintf(b, sizeof(b), tr("%s: %s"), ui_settings_label(SETTINGS_SUM_REAR_TOUCH), a);
  settings_card(0, 1, tr("Touch"), v, ui_settings_label(SETTINGS_SUM_TOUCH), b);

  const char *names[2] = {NULL, NULL};
  int on = ui_settings_tests_on(names, 2);
  if (on == 0) {
    settings_card(1, 1, tr("Tests (beta)"), tr("None"), tr("As in 1.0"), NULL);
  } else {
    snprintf(v, sizeof(v), i18n_plural(on) ? tr("%d options on") : tr("%d option on"), on);
    settings_card(1, 1, tr("Tests (beta)"), v, names[0], names[1]);
  }

  /* the last cell: what X does here */
  float w = COL_W, h = (TH_CONTENT_END - PREVIEW_TOP - GAP) / 2;
  float x = col_x(2), y = PREVIEW_TOP + h + GAP;
  theme_rounded_outline(x, y, w, h, TILE_R, theme_alpha(TH_ACCENT, 160));
  draw_icon_centered(ICON_COG, x + w / 2, y + h / 2 - 14, TH_TEXT);
  theme_text_fit_center(x + w / 2, y + h / 2 + 22, TH_TEXT, TS_SMALL, tr("Open the settings"), (int)w - 24);
}

static void draw_quit_preview(void) {
  preview_title(tr("Quit"), NULL);
  draw_wrapped(AREA_X, PREVIEW_TOP + 24, TS_HINT, TH_TEXT2, tr("Closes VitaMoon and returns to the Vita home screen."),
               (int)AREA_W, 2, 24);
}

/* ---------------- hints ---------------- */

static int home_hints(theme_hint_item *out) {
  int n = 0;
  if (nav.in_tiles) {
    const char *what;
    switch (nav.tile) {
      case HOME_TILE_RESUME: what = resume_button(); break;
      case HOME_TILE_PC: what = view.connected ? tr("Open the library") : tr("PC options"); break;
      case HOME_TILE_ALL: what = tr("Open the library"); break;
      default: what = tr("Play"); break;
    }
    out[n++] = (theme_hint_item){theme_confirm_glyph(), what};
  } else {
    const char *what = nav.menu == HOME_MENU_HOME ? tr("Select") : nav.menu == HOME_MENU_QUIT ? tr("Quit") : tr("Open");
    out[n++] = (theme_hint_item){theme_confirm_glyph(), what};
    if (nav.menu != HOME_MENU_HOME)
      out[n++] = (theme_hint_item){theme_cancel_glyph(), tr("Home")};
  }
  if (!view.connected)
    out[n++] = (theme_hint_item){GLYPH_TRIANGLE, tr("Refresh")};
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
  home_nav_fix(&nav, tiles_on());
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
  if (state == HOST_STATE_OFFLINE || state == HOST_STATE_MOVED)
    ui_host_manage_menu(host);
  else if (app_id)
    ui_connect_paired_device_launch(host, app_id);
  else
    ui_connect_paired_device(host);
  after_page();
}

static void act_tile(int tile) {
  switch (tile) {
    case HOME_TILE_RESUME:
      if (view.host < 0 && !view.connected) {
        before_page(false);
        ui_computers_page();
        after_page();
      } else {
        open_host(view.connected ? view.paused_id : view.resume ? view.resume->id : 0);
      }
      break;
    case HOME_TILE_PC:
      if (view.connected) {
        open_host(0);
      } else if (view.host >= 0) {
        before_page(true);
        ui_host_manage_menu(&known_devices.devices[view.host]);
        after_page();
      }
      break;
    case HOME_TILE_ALL:
      open_host(0);
      break;
    default: {
      int k = tile - HOME_TILE_GAME0;
      if (k >= 0 && k < view.strip_count)
        open_host(view.strip[k]->id);
      break;
    }
  }
}

static void open_settings(void) {
  before_page(true);
  ui_settings_menu();
  after_page();
}

static void quit_app(void) {
  LOGI("app", "quit from the home screen");
  before_page(true);
  if (connection_get_status() != LI_DISCONNECTED)
    connection_terminate();
  exit(0);
}

/* ---------------- touch ---------------- */

typedef struct { float x, y, w, h; } rect;

static bool inside(int tx, int ty, rect r) {
  return tx >= r.x && tx <= r.x + r.w && ty >= r.y && ty <= r.y + r.h;
}

static rect tile_rect(int t) {
  float sx, sw;
  switch (t) {
    case HOME_TILE_RESUME:
      if (view.host < 0 && !view.connected)
        return (rect){AREA_X, AREA_Y, AREA_W, AREA_H};
      return (rect){col_x(0), row_y(0), 2 * COL_W + GAP, 2 * ROW_H + GAP};
    case HOME_TILE_PC: return (rect){col_x(2), row_y(0), COL_W, ROW_H};
    default:
      strip_slot(t == HOME_TILE_ALL ? 3 : t - HOME_TILE_GAME0, &sx, &sw);
      return (rect){sx, row_y(2) + 36, sw, 72};
  }
}

/* A tap focuses what it touches; a tap on what has the focus acts on it. */
static home_action touch_action(int tx, int ty, unsigned tiles) {
  if (tx < DIVIDER_X) {
    for (int m = 0; m < HOME_MENU_COUNT; m++) {
      if (!inside(tx, ty, (rect){MENU_X, menu_row_y(m), MENU_W, MENU_ROW_H}))
        continue;
      bool focused = !nav.in_tiles && nav.menu == m;
      nav.in_tiles = false;
      nav.menu = m;
      return focused ? home_nav_key(&nav, HOME_KEY_CONFIRM, tiles) : HOME_ACT_NONE;
    }
    return HOME_ACT_NONE;
  }
  if (nav.in_tiles || nav.menu == HOME_MENU_HOME) {
    for (int t = 0; t < HOME_TILE_COUNT; t++) {
      if (!home_tile_on(tiles, t) || !inside(tx, ty, tile_rect(t)))
        continue;
      bool focused = nav.in_tiles && nav.tile == t;
      nav.in_tiles = true;
      nav.menu = HOME_MENU_HOME;
      nav.tile = t;
      return focused ? HOME_ACT_TILE : HOME_ACT_NONE;
    }
    return HOME_ACT_NONE;
  }
  /* on another entry's preview: a tap opens that page */
  return inside(tx, ty, (rect){AREA_X, AREA_Y, AREA_W, AREA_H}) ? home_nav_key(&nav, HOME_KEY_CONFIRM, tiles)
                                                               : HOME_ACT_NONE;
}

/* ---------------- the screen ---------------- */

void ui_home(void) {
#ifdef BUILD_ID
  snprintf(version_text, sizeof(version_text), "%s  %s", VERSION_TEXT, BUILD_ID);
#else
  snprintf(version_text, sizeof(version_text), "%s", VERSION_TEXT);
#endif
  load_view();
  nav = (home_nav){true, HOME_MENU_HOME, HOME_TILE_RESUME};
  home_nav_fix(&nav, tiles_on());
  scan_start();
  gui_touch_reset();
  PREVIEW_SCREEN("home");

  /* after the start-up screen, everything below the top bar fades in */
  float appear = splash_take_intro() ? 0.0f : 1.0f;

  while (true) {
    scan_check();
    if (g_host_status_changed && !view.connected)
      g_host_status_changed = 0;   /* seen: the states are read every frame */

    /* ---- input ---- */
    unsigned tiles = tiles_on();
    int buttons = read_buttons();
    bool hold = (buttons & SCE_CTRL_HOLD) != 0;
    home_action act = HOME_ACT_NONE;
    /* arrows repeat while held (as in the other menus); the other buttons act once */
    if (buttons & SCE_CTRL_UP) act = home_nav_key(&nav, HOME_KEY_UP, tiles);
    if (buttons & SCE_CTRL_DOWN) act = home_nav_key(&nav, HOME_KEY_DOWN, tiles);
    if (buttons & SCE_CTRL_LEFT) act = home_nav_key(&nav, HOME_KEY_LEFT, tiles);
    if (buttons & SCE_CTRL_RIGHT) act = home_nav_key(&nav, HOME_KEY_RIGHT, tiles);
    if (!hold && (buttons & config.btn_confirm))
      act = home_nav_key(&nav, HOME_KEY_CONFIRM, tiles);
    else if (!hold && (buttons & config.btn_cancel))
      act = home_nav_key(&nav, HOME_KEY_BACK, tiles);

    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    int tx, ty;
    if (act == HOME_ACT_NONE && gui_touch_tap(&touch, &tx, &ty)) {
      if (ty < 56 && tx < 200 && connection_get_status() == LI_MINIMIZED) {
        /* the top-left corner resumes a paused stream, as on the other screens */
        nav = (home_nav){true, HOME_MENU_HOME, HOME_TILE_RESUME};
        act = HOME_ACT_TILE;
      } else {
        act = touch_action(tx, ty, tiles);
      }
    }

    if (act == HOME_ACT_NONE && !hold) {
      if (buttons & SCE_CTRL_START) {
        open_settings();
        continue;
      }
      if ((buttons & SCE_CTRL_TRIANGLE) && !view.connected) {
        LOGI("home", "refresh: the host scan starts again");
        stop_host_scan_thread();
        scan_start();
      }
      if ((buttons & SCE_CTRL_SQUARE) && view.host >= 0 && !view.connected) {
        before_page(true);
        ui_host_manage_menu(&known_devices.devices[view.host]);
        after_page();
        continue;
      }
    }

    switch (act) {
      case HOME_ACT_LIBRARY: open_host(0); continue;
      case HOME_ACT_COMPUTERS:
        before_page(false);
        ui_computers_page();
        after_page();
        continue;
      case HOME_ACT_SETTINGS: open_settings(); continue;
      case HOME_ACT_QUIT: quit_app(); continue;
      case HOME_ACT_TILE: act_tile(nav.tile); continue;
      default: break;
    }

    /* ---- covers: one per frame ---- */
    covers_load_one();

    /* ---- draw ---- */
    appear = appear + 0.08f > 1.0f ? 1.0f : appear + 0.08f;
    ui_start();
    theme_topbar(NULL);
    theme_set_alpha(appear);
    draw_menu();
    switch (nav.in_tiles ? HOME_MENU_HOME : nav.menu) {
      case HOME_MENU_HOME: draw_dashboard(); break;
      case HOME_MENU_LIBRARY: draw_library_preview(); break;
      case HOME_MENU_COMPUTERS: draw_computers_preview(); break;
      case HOME_MENU_SETTINGS: draw_settings_preview(); break;
      default: draw_quit_preview(); break;
    }
    theme_text(MENU_X, TH_HINTS_Y, TH_TEXT3, TS_MICRO, version_text);
    theme_hint_item hints[6];
    theme_hints(hints, home_hints(hints));
    theme_set_alpha(1.0f);
    ui_end();
  }
}
