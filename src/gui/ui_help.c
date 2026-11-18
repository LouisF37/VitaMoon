/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Help (see ui_help.h). What it says is taken from the code it describes:
 *   L + R + SELECT        gui/quickmenu_logic.h, qm_combo()
 *   START + L + R         input/shortcuts.c (pause: connection_minimize)
 *   START + left          input/shortcuts.c (keyboard)
 *   PS button             input/vita.c, handle_psbutton() (twice quickly: the Vita's home)
 *   rear pad              input/vita.c (top zones L2 / R2, bottom zones L3 / R3)
 *   adding games          Sunshine's own apps.json (Desktop, Steam Big Picture) and the
 *                         labels of its web page (Applications, Add New, Detached
 *                         Commands, Find Cover, Save), in the language shown here
 *   Wi-Fi                 the Vita's Wi-Fi is IEEE 802.11b/g/n on 2.4 GHz; power save
 *                         and Bluetooth as the Computers page reads them (power/vita.c)
 */
#include "ui_help.h"
#include "ui_settings.h"
#include "guilib.h"
#include "theme.h"
#include "preview_hooks.h"
#include "../config.h"
#include "../power/vita.h"
#include "../i18n.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int wifi_power_save = -1;   /* 1 on, 0 off, -1 not known */
static int bluetooth = -1;

void ui_help_refresh(void) {
  wifi_power_save = vitapower_wifi_power_save();
  bluetooth = vitapower_bluetooth();
}

static const char *const k_titles[HELP_COUNT] = {
  N_("During a stream"), N_("Touch"), N_("Adding games"), N_("Wi-Fi"),
};

const char *ui_help_title(int topic) {
  return topic >= 0 && topic < HELP_COUNT ? tr(k_titles[topic]) : "";
}

void ui_help_card(int topic, char *value, size_t value_n, char *line1, size_t n1, char *line2, size_t n2, int *warn) {
  value[0] = line1[0] = line2[0] = 0;
  *warn = 0;
  switch (topic) {
    case HELP_STREAM:
      snprintf(value, value_n, "%s", "L + R + SELECT");
      snprintf(line1, n1, "%s", tr("Quick menu"));
      snprintf(line2, n2, "%s", tr("START + L + R: pause"));
      break;
    case HELP_TOUCH: {
      char mode[64];
      ui_settings_value(SETTINGS_SUM_TOUCH, mode, sizeof(mode));
      snprintf(value, value_n, "%s", mode);
      snprintf(line1, n1, "%s", tr("Front screen"));
      snprintf(line2, n2, "%s", config.rear_touch ? tr("Rear pad: L2 R2 at the top, L3 R3 below") : tr("Rear pad off"));
      break;
    }
    case HELP_GAMES:
      snprintf(value, value_n, "%s", "Sunshine");
      snprintf(line1, n1, "%s", tr("Each game is added once,"));
      snprintf(line2, n2, "%s", tr("in its Applications page"));
      break;
    default:
      if (wifi_power_save == 1) {
        snprintf(value, value_n, "%s", tr("Power save on"));
        snprintf(line1, n1, "%s", tr("It adds lag: turn it off"));
        *warn = 1;
      } else if (bluetooth == 1) {
        snprintf(value, value_n, "%s", tr("Bluetooth on"));
        snprintf(line1, n1, "%s", tr("It can disturb the Wi-Fi"));
        *warn = 1;
      } else {
        snprintf(value, value_n, "%s", tr("Nothing to fix"));
        snprintf(line1, n1, "%s", tr("The Vita: 2.4 GHz only"));
      }
      snprintf(line2, n2, "%s", tr("The PC: a cable if you can"));
      break;
  }
}

/* ---------------- the page ---------------- */

#define HELP_LIST_X    TH_MARGIN
#define HELP_LIST_W    236
#define HELP_PANE_X    (HELP_LIST_X + HELP_LIST_W + 28)   /* 312, as the settings */
#define HELP_PANE_W    (TH_RIGHT - HELP_PANE_X)
#define HELP_ROW_H     44
#define HELP_TOP       TH_CONTENT_Y
#define HELP_KEY_W     172                                  /* the key column */
#define HELP_LINE_H    21

typedef struct {
  const char *key;    /* "" for a paragraph across the pane */
  char text[320];
} help_item;

/* The items of a topic; returns how many. */
static int topic_items(int topic, help_item items[6]) {
  int n = 0;
#define ITEM(KEY, ...) do { items[n].key = (KEY); snprintf(items[n].text, sizeof(items[n].text), __VA_ARGS__); n++; } while (0)
  switch (topic) {
    case HELP_STREAM:
      ITEM("L + R + SELECT", "%s", tr("Quick menu: statistics, keyboard, pause, quit the game."));
      ITEM("START + L + R", "%s", tr("Pause: back to the menus. The game keeps running on the PC."));
      ITEM(tr("START + left"), "%s", tr("Keyboard, to type on the PC."));
      ITEM(tr("PS button"), "%s", config.enable_psbutton_capture
               ? tr("Sent to the PC (Steam's menu, for example). Press it twice quickly for the Vita's home screen.")
               : tr("Kept by the Vita: Settings, Controls, to send it to the PC."));
      break;
    case HELP_TOUCH: {
      char mode[64];
      ui_settings_value(SETTINGS_SUM_TOUCH, mode, sizeof(mode));
      if (config.touchscreen_mode == 0)
        ITEM(tr("Front screen"), "%s", tr("Touchpad: slide to move the pointer, tap to click. Other modes in Settings, Touch."));
      else
        ITEM(tr("Front screen"), tr("%s. The other modes are in Settings, Touch."), mode);
      ITEM(tr("Rear pad"), "%s", config.rear_touch ? tr("Top: L2 and R2. Bottom: L3 and R3. It can be turned off in Settings, Touch.")
                                                    : tr("Off. It can be turned on in Settings, Touch."));
      ITEM(tr("Corners"), "%s", config.enable_front_touchzones
               ? tr("The corners of the front screen are shortcuts (Settings, Touch).")
               : tr("The corners of the front screen can be made shortcuts (Settings, Touch; off at first)."));
      break;
    }
    case HELP_GAMES:
      ITEM("", "%s", tr("VitaMoon shows the applications that Sunshine offers: Desktop and Steam Big Picture at first. Add each game once, on the PC:"));
      ITEM("1", "%s", tr("Open Sunshine's web page (https://localhost:47990), Applications, then Add New."));
      ITEM("2", "%s", tr("Application Name: the game. Detached Commands: steam://rungameid/ then the game's number, from the address of its Steam Store page."));
      ITEM("3", "%s", tr("Find Cover, then Save. The game shows up in the library with its cover and starts as soon as it is chosen."));
      break;
    default:
      ITEM(tr("Power save"), "%s", wifi_power_save == 1 ? tr("On: it adds lag. Turn it off in the Vita's Settings, Power Save Settings.")
                                   : wifi_power_save == 0 ? tr("Off: good.")
                                                          : tr("Keep it off (the Vita's Settings, Power Save Settings): it adds lag."));
      ITEM(tr("Bluetooth"), "%s", bluetooth == 1 ? tr("On: if you use no Bluetooth device, turn it off, it can disturb the Wi-Fi.")
                                   : bluetooth == 0 ? tr("Off: fine.")
                                                    : tr("Turn it off if you use no Bluetooth device: it can disturb the Wi-Fi."));
      ITEM(tr("The Vita"), "%s", tr("Its Wi-Fi uses the 2.4 GHz band only: play close to the box."));
      ITEM(tr("The PC"), "%s", tr("Connect it to the box with a cable if you can."));
      break;
  }
#undef ITEM
  return n;
}

static void draw_page(int topic, float focus_y, float alpha) {
  theme_topbar(NULL);
  theme_header(tr("Help"));

  theme_focus(HELP_LIST_X, focus_y + 2, HELP_LIST_W, HELP_ROW_H - 4);
  for (int t = 0; t < HELP_COUNT; t++) {
    float y = HELP_TOP + t * HELP_ROW_H;
    theme_text_fit(HELP_LIST_X + 18, y + HELP_ROW_H / 2.0f + TS_BODY * 0.36f, t == topic ? TH_TEXT : TH_TEXT2, TS_BODY,
                   ui_help_title(t), HELP_LIST_W - 30);
  }
  theme_rect(HELP_PANE_X - 14, HELP_TOP + 4, 1, TH_CONTENT_END - HELP_TOP - 8, TH_DIVIDER);

  theme_set_alpha(alpha);
  help_item items[6];
  int n = topic_items(topic, items);
  float y = HELP_TOP + 26;
  for (int i = 0; i < n; i++) {
    bool paragraph = !items[i].key[0];
    /* a step number takes a narrow column, a key or a name the wide one */
    float key_w = paragraph ? 0 : strlen(items[i].key) <= 2 ? 30 : HELP_KEY_W;
    float tx = HELP_PANE_X + key_w;
    int tw = (int)(HELP_PANE_W - key_w);
    if (!paragraph)
      theme_text_fit(HELP_PANE_X, y, TH_TEXT, TS_HINT, items[i].key, (int)key_w - 10);
    char lines[4][256];
    int count = theme_wrap(TS_HINT, items[i].text, tw, lines, 4);
#ifdef UI_PREVIEW
    if (count > 3) fprintf(stderr, "HELP TOO LONG (%d lines): %s\n", count, items[i].text);
#endif
    for (int k = 0; k < count && k < 3; k++)
      theme_text(tx, y + k * HELP_LINE_H, paragraph ? TH_TEXT : TH_TEXT2, TS_HINT, lines[k]);
    y += (count < 1 ? 1 : count > 3 ? 3 : count) * HELP_LINE_H + 16;
  }
#ifdef UI_PREVIEW
  if (y - 16 - HELP_LINE_H + 6 > TH_CONTENT_END)
    fprintf(stderr, "HELP TOO LONG (topic %d ends at %.0f)\n", topic, y);
#endif
  theme_set_alpha(1.0f);

  const theme_hint_item hints[] = {
    {theme_cancel_glyph(), tr("Back")},
    {GLYPH_LR, tr("Topic")},
  };
  theme_hints(hints, 2);
}

void ui_help(int topic) {
  PREVIEW_SCREEN("help");
  ui_end();
  gui_touch_reset();
  ui_help_refresh();
  if (topic < 0 || topic >= HELP_COUNT)
    topic = HELP_STREAM;
  float focus_y = HELP_TOP + topic * HELP_ROW_H;
  float alpha = 0.0f;

  while (true) {
    focus_y = theme_approach(focus_y, HELP_TOP + topic * HELP_ROW_H);
    alpha = alpha + 0.2f > 1.0f ? 1.0f : alpha + 0.2f;
    ui_start();
    draw_page(topic, focus_y, alpha);

    int buttons = read_buttons();
    bool held = (buttons & SCE_CTRL_HOLD) != 0;
    int next = topic;
    if (buttons & SCE_CTRL_DOWN) next = topic < HELP_COUNT - 1 ? topic + 1 : topic;
    if (buttons & SCE_CTRL_UP) next = topic > 0 ? topic - 1 : topic;
    if ((buttons & SCE_CTRL_LTRIGGER) && !held) next = (topic + HELP_COUNT - 1) % HELP_COUNT;
    if ((buttons & SCE_CTRL_RTRIGGER) && !held) next = (topic + 1) % HELP_COUNT;

    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    int tx, ty;
    bool back = (buttons & config.btn_cancel) && !held;
    if (gui_touch_tap(&touch, &tx, &ty)) {
      if (tx < HELP_LIST_X + HELP_LIST_W && ty >= HELP_TOP && ty < HELP_TOP + HELP_COUNT * HELP_ROW_H)
        next = (ty - HELP_TOP) / HELP_ROW_H;
      else if (ty < TH_HEADER_LINE && tx < 200)
        back = true;   /* the title's back arrow */
    }
    if (next != topic) {
      topic = next;
      alpha = 0.0f;
    }
    ui_end();
    if (back)
      break;
  }
  ui_end();
}
