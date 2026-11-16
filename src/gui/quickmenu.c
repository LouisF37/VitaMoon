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

static char host_name[64] = "";
static char game_name[128] = "";

void quickmenu_set_context(const char *host, const char *game) {
  snprintf(host_name, sizeof(host_name), "%s", host ? host : "");
  snprintf(game_name, sizeof(game_name), "%s", game ? game : "");
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

#define QM_X      TH_MARGIN
#define QM_Y      80
#define QM_W      480
#define QM_HEAD   76
#define QM_ROW_H  44
#define QM_H      (QM_HEAD + 10 + QM_COUNT * QM_ROW_H + 64)

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
  theme_text_fit(x + 24, y + 38, TH_TEXT, TS_LEAD, game_name[0] ? game_name : tr("Game"), w - 48);
  theme_text_fit(x + 24, y + 60, TH_TEXT3, TS_SMALL, host_name, w - 48);
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
