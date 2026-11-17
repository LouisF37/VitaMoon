/*
 * In-game quick menu (quickmenu.h): drawing and actions. The state machine is
 * in quickmenu_logic.h. The input thread owns the state; the display thread
 * only reads the copy published after each sample.
 */
#include "../log.h"
#include "quickmenu.h"
#include "quickmenu_logic.h"
#include "theme.h"
#include "guilib.h"
#include "../config.h"
#include "../connection.h"
#include "../debug.h"
#include "../i18n.h"
#include "../keyboardsystem.h"
#include "../video/latency_test.h"
#include "../video/stream_stats.h"

#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>

#include <stdio.h>
#include <string.h>

static qm_state st;

/* What the display thread draws, published by the input thread. */
static volatile bool shown_open = false;
static volatile int shown_selected = 0;
static volatile bool shown_confirm_quit = false;

/* Requests for the menu thread. */
static volatile bool quit_request = false;
static volatile bool config_changed = false;

#define QM_X      TH_MARGIN
#define QM_Y      80
#define QM_W      480
#define QM_HEAD   76
#define QM_ROW_H  44
#define QM_H      (QM_HEAD + 10 + QM_COUNT * QM_ROW_H + 64)
#define QM_NAME_W (QM_W - 48)

/* The names in the header, already cut to QM_NAME_W (quickmenu_set_context). */
static char host_name[64] = "";
static char game_name[128] = "";

void quickmenu_set_context(const char *host, const char *game) {
  // Called on the menu thread before the stream: the names the menu will draw
  // over the game are cut to its width and their glyphs rendered here, so that
  // the display thread only draws them during play.
  theme_ellipsize(TS_LEAD, game && game[0] ? game : tr("Game"), QM_NAME_W, game_name, sizeof(game_name));
  theme_ellipsize(TS_SMALL, host ? host : "", QM_NAME_W, host_name, sizeof(host_name));
  theme_prepare_text(TS_LEAD, game_name);
  theme_prepare_text(TS_SMALL, host_name);
  // The second line of the header changes during play (header_info): its
  // characters are rendered here too.
  char sample[160];
  snprintf(sample, sizeof(sample), "%s 0123456789 · %s %s %s %s", host_name, tr("%u min streaming"),
           tr("%d h %02d of battery"), tr("%d min of battery"), tr("charging"));
  theme_prepare_text(TS_SMALL, sample);
  theme_prepare_text(TS_BODY, tr("Measure the delay (test page)"));
  theme_prepare_text(TS_BODY, tr("Stop the delay test"));
  latency_test_prepare_text();
}

/* Second line of the header: the PC, how long this stream has been running,
 * the battery time left as the Vita estimates it (its percentage and the clock
 * are in the top bar). Display thread; the battery is read every 5 s. */
static void header_info(char *out, size_t n) {
  static uint64_t read_at = 0;
  static int minutes_left = -1;
  static bool charging = false;
  uint64_t now = sceKernelGetProcessTimeWide();
  if (read_at == 0 || now - read_at > 5000000) {
    read_at = now;
    charging = scePowerIsBatteryCharging();
    minutes_left = scePowerGetBatteryLifeTime();
  }
  char played[48], battery[64];
  snprintf(played, sizeof(played), tr("%u min streaming"), stream_stats_elapsed_s() / 60);
  if (charging)
    snprintf(battery, sizeof(battery), "%s", tr("charging"));
  else if (minutes_left >= 60)
    snprintf(battery, sizeof(battery), tr("%d h %02d of battery"), minutes_left / 60, minutes_left % 60);
  else if (minutes_left >= 0)
    snprintf(battery, sizeof(battery), tr("%d min of battery"), minutes_left);
  else
    battery[0] = 0;
  if (battery[0])
    snprintf(out, n, "%s  ·  %s  ·  %s", host_name, played, battery);
  else
    snprintf(out, n, "%s  ·  %s", host_name, played);
}

static void publish(void) {
  shown_selected = st.selected;
  shown_confirm_quit = st.confirm_quit;
  shown_open = st.open;
}

void quickmenu_reset(void) {
  memset(&st, 0, sizeof(st));
  publish();
}

bool quickmenu_handle_input(uint32_t buttons) {
  if (!st.open && !st.wait_release && qm_combo(buttons)) {
    qm_open(&st, buttons);
    LOGI("menu", "quick menu opened");
  }
  if (st.open) {
    qm_action action = qm_input(&st, buttons, config.btn_confirm, config.btn_cancel);
    publish();
    switch (action) {
      case QM_ACT_CLOSE:
        LOGI("menu", "quick menu: back to the game");
        break;
      case QM_ACT_TOGGLE_STATS:
        config.show_fps = !config.show_fps;
        config_changed = true;
        break;
      case QM_ACT_KEYBOARD:
        LOGI("menu", "quick menu: keyboard");
        keyboardsystem_open_keyboard();
        break;
      case QM_ACT_LATENCY:
        latency_test_toggle();
        break;
      case QM_ACT_PAUSE:
        LOGI("menu", "quick menu: pause");
        connection_minimize();
        break;
      case QM_ACT_QUIT:
        LOGI("menu", "quick menu: quit the game");
        quit_request = true;
        connection_minimize();
        break;
      default:
        break;
    }
  }
  bool blocks = qm_blocks(&st, buttons);
  publish();
  return blocks;
}

bool quickmenu_take_quit_request(void) {
  bool r = quit_request;
  quit_request = false;
  return r;
}

bool quickmenu_take_config_changed(void) {
  bool r = config_changed;
  config_changed = false;
  return r;
}

/* ---------------- drawing ---------------- */


void quickmenu_draw(void) {
  if (!shown_open)
    return;
  int selected = shown_selected;
  bool confirm_quit = shown_confirm_quit;

  theme_set_alpha(1.0f);
  theme_rect(0, 0, WIDTH, HEIGHT, theme_alpha(TH_INK, 150));   /* the game, dimmed */
  theme_topbar(NULL);                                           /* Wi-Fi, battery, clock */

  const int x = QM_X, y = QM_Y, w = QM_W, h = QM_H;
  theme_glass(x, y, w, h, 14);
  theme_text(x + 24, y + 38, TH_TEXT, TS_LEAD, game_name);
  char info[192];
  header_info(info, sizeof(info));
  theme_text_fit(x + 24, y + 60, TH_TEXT3, TS_SMALL, info, QM_NAME_W);
  theme_rect(x + 24, y + QM_HEAD, w - 48, 1, TH_DIVIDER);

  for (int i = 0; i < QM_COUNT; i++) {
    float ry = y + QM_HEAD + 10 + i * QM_ROW_H;
    float cy = ry + (QM_ROW_H - 4) / 2.0f;
    if (i == selected)
      theme_focus(x + 12, ry, w - 24, QM_ROW_H - 4);
    const char *label = "";
    const char *shortcut = NULL;
    unsigned int color = TH_TEXT;
    switch (i) {
      case QM_RESUME: label = tr("Back to the game"); break;
      case QM_STATS: label = tr("Statistics while streaming"); break;
      case QM_KEYBOARD: label = tr("Keyboard"); shortcut = tr("START + Left"); break;
      case QM_LATENCY:
        label = latency_test_running() ? tr("Stop the delay test") : tr("Measure the delay (test page)");
        break;
      case QM_PAUSE: label = tr("Pause: library and settings"); shortcut = "START + L + R"; break;
      case QM_QUIT:
        label = confirm_quit ? tr("Quit the game? Press again.") : tr("Quit game");
        color = confirm_quit ? TH_WARN : TH_TEXT;
        break;
    }
    int right = x + w - 28;
    int label_max = w - 56;
    if (i == QM_STATS) {
      theme_toggle(right - 44, cy, config.show_fps ? 1.0f : 0.0f);
      label_max -= 60;
    } else if (shortcut) {
      int sw = theme_text_width(TS_SMALL, shortcut);
      theme_text_right(right, cy + 5, TH_TEXT3, TS_SMALL, shortcut);
      label_max -= sw + 16;
    }
    theme_text_fit(x + 28, cy + 6, color, TS_BODY, label, label_max);
  }

  theme_hint_item hints[2] = {
    {theme_confirm_glyph(), tr("OK")},
    {theme_cancel_glyph(), tr("Back to the game")},
  };
  theme_hints_at(hints, 2, x + w / 2.0f, y + h - 22);
}
