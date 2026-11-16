#include "../log.h"
#include "ui_settings.h"
#include "theme.h"

#include "guilib.h"
#include "ime.h"

#include "../config.h"
#include "../input/vita.h"
#include "../input/swap_shoulder_buttons.h"
#include "../video/vita.h"
#include "../debug.h"
#include "../input/touchabsolute.h"
#include "../input/keyboardkeys.h"
#include "../i18n.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <psp2/videodec.h>
#include <vita2d.h>
#include <Limelight.h>
extern char* strdup(const char*);

static unsigned int settings_special_codes[] = {0,
  // special
  INPUT_TYPE_DEF_NAME | INPUT_TYPE_SPECIAL,
  INPUT_SPECIAL_KEY_PAUSE | INPUT_TYPE_SPECIAL,
  INPUT_SPECIAL_KEY_KEYBOARD | INPUT_TYPE_SPECIAL,
  // gamepad
  INPUT_TYPE_DEF_NAME | INPUT_TYPE_GAMEPAD,
  SPECIAL_FLAG | INPUT_TYPE_GAMEPAD,
  LB_FLAG | INPUT_TYPE_GAMEPAD,
  RB_FLAG | INPUT_TYPE_GAMEPAD,
  LS_CLK_FLAG | INPUT_TYPE_GAMEPAD,
  RS_CLK_FLAG | INPUT_TYPE_GAMEPAD,
  LEFT_TRIGGER | INPUT_TYPE_ANALOG,
  RIGHT_TRIGGER | INPUT_TYPE_ANALOG,
  // mouse
  INPUT_TYPE_DEF_NAME | INPUT_TYPE_MOUSE,
  BUTTON_LEFT | INPUT_TYPE_MOUSE,
  BUTTON_RIGHT | INPUT_TYPE_MOUSE,
  BUTTON_MIDDLE | INPUT_TYPE_MOUSE,
  BUTTON_X1 | INPUT_TYPE_MOUSE,
  BUTTON_X2 | INPUT_TYPE_MOUSE,
  // keyboard
  INPUT_TYPE_DEF_NAME | INPUT_TYPE_KEYBOARD,
  27,    73,  77,  9,
  112,  113,  114,  115,  116,  117,  118,  119, 120,   121,   122,   123
};

// Display names of the special inputs, aligned with settings_special_codes.
// Stored in English; translated with tr() at display time.
char *settings_special_names[] = {
  N_("None"),
  // special
  N_("Special inputs"),
  N_("Pause stream"),
  N_("Open keyboard"),
  // gamepad
  N_("Gamepad buttons"),
  N_("Special (XBox button)"),
  "LB", "RB", "LS", "RS", "LT", "RT",
  // mouse
  N_("Mouse buttons"),
  N_("Left"),
  N_("Right"),
  N_("Middle(wheel)"),
  "X1(4th)",
  "X2(5th)",
  // keyboard
  N_("Keyboard input codes"),
  "Esc",
  "I", "M", "Tab",
  "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
  NULL
};

#define MAX_RESOLUTION 9
static int RESOLUTIONS[MAX_RESOLUTION][2] = {
  {960, 540},   // 16:9, QHD
  {960, 544},   // VITA original
  {1024, 576},  // 16:9
  {1152, 648},  // 16:9
  {1280, 540},  // 21:9
  {1280, 720},  // 16:9, 720p HD
  {1366, 768},  // 16:9, WXGA
  {1600, 900},  // 16:9, 900p HD+
  //{1720, 720},  // 21:9
  {1920, 1080}, // 16:9, FHD
  //{1920, 1200}, // 16:10
};

int support_resolution_idx[MAX_RESOLUTION] = {-1};
char *support_resolutions[MAX_RESOLUTION] = {0};
int support_resolution_count = 0;


/*
 * Deadzone
 */

/* Pages with a list on the left and a scale drawing on the right (rear pad
 * dead zone, corner shortcuts). */
#define SIDE_LIST_W  500
#define SIDE_CARD_X  (TH_MARGIN + SIDE_LIST_W + 24)
#define SIDE_CARD_W  (TH_RIGHT - SIDE_CARD_X)
#define SIDE_CARD_Y  (TH_CONTENT_Y + 2)
#define SIDE_DRAW_W  (SIDE_CARD_W - 32)
#define SIDE_DRAW_H  (SIDE_DRAW_W * HEIGHT / WIDTH)
#define SIDE_SCALE   (SIDE_DRAW_W / (float)WIDTH)

static menu_geom side_list_geom(void) {
  menu_geom geom = {0};
  geom.page = true;
  geom.x = TH_MARGIN;
  geom.width = SIDE_LIST_W;
  geom.y = TH_CONTENT_Y;
  geom.el = 44;
  geom.height = (TH_CONTENT_END - geom.y) / geom.el * geom.el;
  geom.total_y = geom.y + geom.height;
  return geom;
}

/* Card holding a scale drawing of the screen (or of the rear pad) above a
 * caption. Gives back the top-left corner of the drawing. */
static void side_card(const char *caption, float *ox, float *oy) {
  char lines[3][256];
  int n = theme_wrap(TS_SMALL, caption, SIDE_DRAW_W, lines, 3);
  theme_glass(SIDE_CARD_X, SIDE_CARD_Y, SIDE_CARD_W, 16 + SIDE_DRAW_H + 16 + n * 19 + 10, 14);
  *ox = SIDE_CARD_X + 16;
  *oy = SIDE_CARD_Y + 16;
  theme_rounded_rect(*ox, *oy, SIDE_DRAW_W, SIDE_DRAW_H, 6, theme_alpha(TH_INK, 150));
  theme_rounded_outline(*ox, *oy, SIDE_DRAW_W, SIDE_DRAW_H, 6, TH_DIVIDER);
  for (int i = 0; i < n; i++)
    theme_text(*ox, *oy + SIDE_DRAW_H + 30 + i * 19, TH_TEXT2, TS_SMALL, lines[i]);
}

/*
 * Rear touch pad dead zone
 */

#define DEADZONE_MIN_ACTIVE 90   /* width and height the active zone always keeps */

static void deadzone_draw() {
  float ox, oy;
  side_card(tr("Touch the rear pad: a dot shows each touch that counts."), &ox, &oy);

  /* active zone, split into the four areas that act as buttons */
  float x = ox + config.back_deadzone.left * SIDE_SCALE;
  float y = oy + config.back_deadzone.top * SIDE_SCALE;
  float w = (WIDTH - config.back_deadzone.left - config.back_deadzone.right) * SIDE_SCALE;
  float h = (HEIGHT - config.back_deadzone.top - config.back_deadzone.bottom) * SIDE_SCALE;
  if (w >= 2 && h >= 2) {
    theme_rect(x, y, w, h, theme_alpha(TH_ACCENT, 56));
    theme_rect(x, y, w, 1, TH_ACCENT);
    theme_rect(x, y + h - 1, w, 1, TH_ACCENT);
    theme_rect(x, y, 1, h, TH_ACCENT);
    theme_rect(x + w - 1, y, 1, h, TH_ACCENT);
    theme_rect(x + w / 2, y + 1, 1, h - 2, RGBA8(255, 255, 255, 80));
    theme_rect(x + 1, y + h / 2, w - 2, 1, RGBA8(255, 255, 255, 80));
  }

  SceTouchData touch_data;
  memset(&touch_data, 0, sizeof(touch_data));
  sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch_data, 1);

  for (int i = 0; i < touch_data.reportNum; i++) {
    int tx = touch_data.report[i].x * WIDTH / 1920;
    int ty = touch_data.report[i].y * HEIGHT / 1088;
    if (tx < config.back_deadzone.left || tx > WIDTH - config.back_deadzone.right)
      continue;

    if (ty < config.back_deadzone.top || ty > HEIGHT - config.back_deadzone.bottom)
      continue;

    theme_circle(ox + tx * SIDE_SCALE, oy + ty * SIDE_SCALE, 6, TH_TEXT);
  }
}

/* ids: 0 top, 1 left, 2 bottom, 3 right (the original labels "Left" and
 * "Right" each changed the other side). */
static int *deadzone_side(int id) {
  switch (id) {
    case 0: return &config.back_deadzone.top;
    case 1: return &config.back_deadzone.left;
    case 2: return &config.back_deadzone.bottom;
    default: return &config.back_deadzone.right;
  }
}

static int deadzone_loop(int cursor, void *context, const input_data *input) {
  menu_entry *menu = context;

  bool left = input->buttons & SCE_CTRL_LEFT;
  bool right = input->buttons & SCE_CTRL_RIGHT;

  int delta = left ? -15 : (right ? 15 : 0);
  if (delta) {
    /* a side stops growing when the active zone would get too small */
    bool horizontal = cursor == 1 || cursor == 3;
    int *side = deadzone_side(cursor);
    int *opposite = deadzone_side((cursor + 2) % 4);
    int max = (horizontal ? WIDTH : HEIGHT) - *opposite - DEADZONE_MIN_ACTIVE;
    *side += delta;
    if (*side > max) *side = max;
    if (*side < 0) *side = 0;
  }

  for (int i = 0; i < 4; i++)
    snprintf(menu[i].subname, sizeof(menu[i].subname), "%d px", *deadzone_side(menu[i].id));

  return 0;
}

static int deadzone_settings_menu() {
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

  menu_entry menu[4];
  int idx = 0;
  menu[idx++] = (menu_entry) { .name = tr("Top"), .disabled = false, .id = 0, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = tr("Left"), .disabled = false, .id = 1, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = tr("Bottom"), .disabled = false, .id = 2, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = tr("Right"), .disabled = false, .id = 3, .suffix = ICON_LEFT_RIGHT_ARROWS };
  for (int i = 0; i < 4; i++)
    snprintf(menu[i].subname, sizeof(menu[i].subname), "%d px", *deadzone_side(menu[i].id));

  theme_hint_item hints[2] = {
    {GLYPH_DPAD_LR, tr("Change")},
    {theme_cancel_glyph(), tr("Back")},
  };
  menu_options options = {0};
  options.title = tr("Rear touch pad dead zone");
  options.hints = hints;
  options.hint_count = 2;
  gui_set_next_options(&options);

  menu_geom geom = side_list_geom();
  return display_menu(menu, idx, &geom, &deadzone_loop, NULL, &deadzone_draw, &menu);
}

/*
 * Special keys
 */

static void special_keys_name(int ord, char *text) {
  if (ord == 0) {
    strcpy(text, tr("None"));
    return;
  }

  bool did_find = false;
  int i = 0;
  for (; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    if (settings_special_codes[i] == ord) {
      did_find = true;
      break;
    }
  }

  if (did_find) {
    strcpy(text, tr(settings_special_names[i]));
  } else {
    sprintf(text, "%d", ord);
  }
}

enum {
  SETTINGS_SELECT_SPECIAL_KEY_MANUAL = -1000
};

enum {
  SETTINGS_SPECIAL_KEYS_OFFSET,
  SETTINGS_SPECIAL_KEYS_SIZE
};

enum {
  SETTINGS_SPECIAL_KEYS_NW_VIEW = 3
};

static int select_special_key_loop(int id, void *context, const input_data *input) {
  int *code = context;

  if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
    return 0;
  }
  if (id != SETTINGS_SELECT_SPECIAL_KEY_MANUAL) {
    *code = id;
    return 1;
  }
  char key_code_value[512];
  if (ime_dialog_number(key_code_value, tr("Enter key code:"), "") == 0) {
      int key_code = atoi(key_code_value);
      if (key_code) {
        *code = key_code;
        return 1;
      } else {
        display_error(tr("Incorrect key code entered: %s"), key_code_value);
      }
  }

  return 0;
}

static int select_special_key_menu(int *code) {
  // TODO: sizeof(codes) / sizeof(int) ?
  menu_entry menu[64];
  int idx = 0;
  for (int i = 0; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    unsigned int id = settings_special_codes[i];
    menu[idx++] = (menu_entry) {
      .id = id,
      .name = malloc(sizeof(char) * 256),
      .disabled = (id >= INPUT_TYPE_DEF_NAME),
      .separator = (id >= INPUT_TYPE_DEF_NAME)   /* group names are headings */
    };
    special_keys_name(id, menu[idx-1].name);
  }

  menu[idx++] = (menu_entry) { .name = "", .disabled = true, .separator = true };
  menu[idx++] = (menu_entry) { .name = tr("Enter manually ..."), .id = SETTINGS_SELECT_SPECIAL_KEY_MANUAL };

  gui_set_next_title(tr("Choose a key or an action"));
  int return_code = display_menu(menu, idx, NULL, &select_special_key_loop, NULL, NULL, code);
  for (int i = 0; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    free(menu[i].name);
  }

  return return_code;
}

/* Row under the cursor of the corner shortcuts page (set by its loop). */
static int special_keys_focus = -1;

static int special_keys_loop(int id, void *context, const input_data *input) {
  special_keys_focus = id;
  bool left = input->buttons & SCE_CTRL_LEFT;
  bool right = input->buttons & SCE_CTRL_RIGHT;
  int selected_ord = -1;
  int delta = left ? -15 : (right ? 15 : 0);

  switch (id) {
    case SETTINGS_SPECIAL_KEYS_OFFSET:
      config.special_keys.offset += delta;
      if (config.special_keys.offset < 0) config.special_keys.offset = 0;
      if (config.special_keys.offset > 180) config.special_keys.offset = 180;
      break;
    case SETTINGS_SPECIAL_KEYS_SIZE:
      config.special_keys.size += delta;
      if (config.special_keys.size < 30) config.special_keys.size = 30;
      if (config.special_keys.size > 240) config.special_keys.size = 240;
      break;
    default:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      select_special_key_menu(&selected_ord);
      if (selected_ord != -1) {
        switch (id) {
          case TOUCHSEC_SPECIAL_NW:
            config.special_keys.nw = selected_ord;
            break;
          case TOUCHSEC_SPECIAL_NE:
            config.special_keys.ne = selected_ord;
            break;
          case TOUCHSEC_SPECIAL_SW:
            config.special_keys.sw = selected_ord;
            break;
          case TOUCHSEC_SPECIAL_SE:
            config.special_keys.se = selected_ord;
            break;
        }
      }
      break;
  }

  menu_entry *menu = context;

  int idx = 0;
  sprintf(menu[idx++].subname, "%d px", config.special_keys.offset);
  sprintf(menu[idx++].subname, "%d px", config.special_keys.size);

  idx++;
  special_keys_name(config.special_keys.nw, menu[idx++].subname);
  special_keys_name(config.special_keys.ne, menu[idx++].subname);
  special_keys_name(config.special_keys.sw, menu[idx++].subname);
  special_keys_name(config.special_keys.se, menu[idx++].subname);

  return 0;
}

static void special_keys_draw() {
  float ox, oy;
  side_card(tr("Zones of the front screen that trigger a shortcut during a stream."), &ox, &oy);

  float size = config.special_keys.size * SIDE_SCALE;
  float offset = config.special_keys.offset * SIDE_SCALE;
  const int ids[4] = {TOUCHSEC_SPECIAL_NW, TOUCHSEC_SPECIAL_NE, TOUCHSEC_SPECIAL_SW, TOUCHSEC_SPECIAL_SE};
  const int codes[4] = {config.special_keys.nw, config.special_keys.ne,
                        config.special_keys.sw, config.special_keys.se};
  for (int i = 0; i < 4; i++) {
    float x = (i % 2 == 0) ? ox + offset : ox + SIDE_DRAW_W - offset - size;
    float y = (i < 2) ? oy + offset : oy + SIDE_DRAW_H - offset - size;
    /* the corner being edited is solid, corners with a shortcut are filled */
    if (special_keys_focus == ids[i]) {
      theme_rect(x, y, size, size, TH_ACCENT);
    } else {
      theme_rect(x, y, size, size, theme_alpha(TH_ACCENT, codes[i] ? 96 : 30));
      theme_rect(x, y, size, 1, theme_alpha(TH_ACCENT, 170));
      theme_rect(x, y + size - 1, size, 1, theme_alpha(TH_ACCENT, 170));
      theme_rect(x, y, 1, size, theme_alpha(TH_ACCENT, 170));
      theme_rect(x + size - 1, y, 1, size, theme_alpha(TH_ACCENT, 170));
    }
  }
}

/* Left/right change the two sizes; the corners open the list of actions. */
static int special_keys_hints(int id, theme_hint_item *out, int max) {
  int n = 0;
  if (id == SETTINGS_SPECIAL_KEYS_OFFSET || id == SETTINGS_SPECIAL_KEYS_SIZE)
    out[n++] = (theme_hint_item){GLYPH_DPAD_LR, tr("Change")};
  else
    out[n++] = (theme_hint_item){theme_confirm_glyph(), tr("Choose")};
  if (n < max)
    out[n++] = (theme_hint_item){theme_cancel_glyph(), tr("Back")};
  return n;
}

static int special_keys_menu() {
  menu_entry menu[16];
  int idx = 0;

  menu[idx++] = (menu_entry) { .name = tr("Distance from the corner"), .id = SETTINGS_SPECIAL_KEYS_OFFSET, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = tr("Size"), .id = SETTINGS_SPECIAL_KEYS_SIZE, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = tr("Assignments"), .disabled = true, .separator = true };
  menu[idx++] = (menu_entry) { .name = tr("Top left"), .id = TOUCHSEC_SPECIAL_NW };
  menu[idx++] = (menu_entry) { .name = tr("Top right"), .id = TOUCHSEC_SPECIAL_NE };
  menu[idx++] = (menu_entry) { .name = tr("Bottom left"), .id = TOUCHSEC_SPECIAL_SW };
  menu[idx++] = (menu_entry) { .name = tr("Bottom right"), .id = TOUCHSEC_SPECIAL_SE };

  menu_options options = {0};
  options.title = tr("Corner shortcuts");
  options.hints_for = special_keys_hints;
  gui_set_next_options(&options);

  special_keys_focus = -1;
  menu_geom geom = side_list_geom();
  return display_menu(menu, idx, &geom, &special_keys_loop, NULL, &special_keys_draw, &menu);
}


/*
 * Settings screen
 *
 * Two panes, as in VitaRPS5: categories on the left, the rows of the focused
 * category on the right, the description of the focused row underneath.
 *   up/down     move        left/right  change the value
 *   X           open the category / toggle / next value / run the action
 *   O           back to the categories, then leave (settings are saved)
 *   L / R       previous / next category
 * Rows are described by one table (k_rows); what a row shows and does lives in
 * row_value(), row_is_on(), row_enabled() and row_change(). Labels are
 * translated when drawn, so a language change shows at once.
 */

enum {
  CAT_GENERAL, CAT_VIDEO, CAT_PERFORMANCE, CAT_CONTROLS, CAT_TOUCH, CAT_ADVANCED, CAT_COUNT
};

static const char *k_categories[CAT_COUNT] = {
  N_("General"), N_("Video"), N_("Performance"), N_("Controls"), N_("Touch"), N_("Advanced")
};

typedef enum { ROW_CHOICE, ROW_TOGGLE, ROW_ACTION } row_type;

enum {
  S_LANGUAGE, S_THEME, S_SHOW_STATS, S_SWAP_XO, S_KEEP_AWAKE, S_AUTO_RECONNECT, S_DEBUG_LOG,
  S_RESOLUTION, S_FPS, S_BITRATE, S_CROP, S_LOCAL_AUDIO,
  S_PROFILE, S_DISPLAY_MODE, S_LL_AUDIO, S_CATCHUP, S_YUV, S_REORDER, S_BOOST, S_MATCH_REFRESH,
  S_PACKET_SIZE,
  S_CONTROLLER_TYPE, S_INPUT_SYNC, S_SWAP_SHOULDER, S_MAPPING, S_PS_CAPTURE, S_DOUBLE_TAP_SPRINT, S_SPRINT_TIME,
  S_MOTION, S_MOTION_X, S_MOTION_Y, S_KEYBOARD_LAYOUT,
  S_TOUCH_MODE, S_MOUSE_ACCEL, S_SPECIAL_KEYS, S_SPECIAL_KEYS_SETUP, S_DEADZONE,
  S_REMOTE, S_RFI, S_SOPS, S_FRAME_PACER, S_VBLANK, S_PINNING, S_KEEPALIVE, S_MSAA
};

typedef struct {
  int id;
  int category;
  row_type type;
  const char *label;
  const char *help;
} setting_row;

static const setting_row k_rows[] = {
  /* General */
  {S_LANGUAGE, CAT_GENERAL, ROW_CHOICE, N_("Language"),
   N_("Interface language. Takes effect immediately.")},
  {S_THEME, CAT_GENERAL, ROW_CHOICE, N_("Theme"),
   N_("Colours and background of the menus. Takes effect immediately.")},
  {S_SHOW_STATS, CAT_GENERAL, ROW_TOGGLE, N_("Statistics while streaming"),
   N_("Frames, delay, network and sound at the top of the screen, updated every second.")},
  {S_SWAP_XO, CAT_GENERAL, ROW_TOGGLE, N_("Swap X and O in the menus"),
   N_("O confirms and X goes back, as on Japanese consoles.")},
  {S_KEEP_AWAKE, CAT_GENERAL, ROW_TOGGLE, N_("Keep the Vita awake"),
   N_("The screen does not dim and the Vita does not go to sleep during a stream.")},
  {S_AUTO_RECONNECT, CAT_GENERAL, ROW_TOGGLE, N_("Reconnect automatically"),
   N_("After a Wi-Fi drop or a sleep during a stream, the game starts again on its own.")},
  {S_DEBUG_LOG, CAT_GENERAL, ROW_TOGGLE, N_("Detailed log"),
   N_("Adds every detail to vitamoon.log, which always keeps the main events. For troubleshooting.")},

  /* Video */
  {S_RESOLUTION, CAT_VIDEO, ROW_CHOICE, N_("Resolution"),
   N_("960 x 544 is the Vita screen itself: the sharpest picture and the lightest stream.")},
  {S_FPS, CAT_VIDEO, ROW_CHOICE, N_("Frame rate"),
   N_("60 matches the screen. 30 halves the data on a weak Wi-Fi link.")},
  {S_BITRATE, CAT_VIDEO, ROW_CHOICE, N_("Bitrate"),
   N_("Auto suits the Vita's Wi-Fi. Lower it if frames get lost. X: type a value.")},
  {S_CROP, CAT_VIDEO, ROW_TOGGLE, N_("Crop to fill the screen"),
   N_("When the PC picture is not 16:9, shows its centre instead of black bars.")},
  {S_LOCAL_AUDIO, CAT_VIDEO, ROW_TOGGLE, N_("Sound on the PC too"),
   N_("The PC keeps playing the sound that is sent to the Vita.")},

  /* Performance */
  {S_PROFILE, CAT_PERFORMANCE, ROW_CHOICE, N_("Profile"),
   N_("Sets the options below at once. Smooth is the default; Original is the app as it was before.")},
  {S_DISPLAY_MODE, CAT_PERFORMANCE, ROW_CHOICE, N_("Display"),
   N_("Smooth: one frame per refresh. Paced: frames at the PC's pace, a little later. Low latency: newest at once.")},
  {S_LL_AUDIO, CAT_PERFORMANCE, ROW_TOGGLE, N_("Low-latency sound"),
   N_("5 ms sound blocks and a buffer that adapts to the Wi-Fi link. Off: original 20 ms blocks.")},
  {S_CATCHUP, CAT_PERFORMANCE, ROW_TOGGLE, N_("Decoder catch-up"),
   N_("Takes back the frames the video decoder sometimes keeps: each one is 17 ms of delay.")},
  {S_YUV, CAT_PERFORMANCE, ROW_TOGGLE, N_("Fast colour decoding"),
   N_("The graphics chip converts the colours instead of the decoder: about 2 ms less per frame.")},
  {S_REORDER, CAT_PERFORMANCE, ROW_CHOICE, N_("Wait for late packets"),
   N_("Wi-Fi delivers some packets out of order. Waiting for them avoids a frozen picture.")},
  {S_BOOST, CAT_PERFORMANCE, ROW_TOGGLE, N_("Maximum CPU and GPU speed"),
   N_("CPU at 444 MHz and GPU at 222 MHz while streaming. Uses a little more battery.")},
  {S_MATCH_REFRESH, CAT_PERFORMANCE, ROW_TOGGLE, N_("Match the Vita screen rate"),
   N_("The PC encodes at the exact rate of the Vita screen, which avoids regular skipped frames.")},
  {S_PACKET_SIZE, CAT_PERFORMANCE, ROW_CHOICE, N_("Video packet size"),
   N_("1024, Moonlight's value, lost the fewest packets on a Vita. Larger: fewer packets per frame.")},

  /* Controls */
  {S_CONTROLLER_TYPE, CAT_CONTROLS, ROW_CHOICE, N_("Controller seen by the PC"),
   N_("Games show the button symbols of this controller.")},
  {S_INPUT_SYNC, CAT_CONTROLS, ROW_TOGGLE, N_("Read the controls in step with the Vita"),
   N_("Reads the buttons once per sample of the Vita instead of every 2 ms. If touch feels slower, turn it off.")},
  {S_SWAP_SHOULDER, CAT_CONTROLS, ROW_TOGGLE, N_("Swap L1/R1 with L2/R2"),
   N_("L and R send the triggers, the rear touch pad sends L1 and R1. Turns the mapping file off.")},
  {S_MAPPING, CAT_CONTROLS, ROW_TOGGLE, N_("Button mapping file"),
   N_("Uses mappings/vita.conf from VitaMoon's folder. Turns the swap above off.")},
  {S_PS_CAPTURE, CAT_CONTROLS, ROW_TOGGLE, N_("PS button sent to the PC"),
   N_("The PS button is sent to the game instead of opening the Vita home screen.")},
  {S_DOUBLE_TAP_SPRINT, CAT_CONTROLS, ROW_TOGGLE, N_("Double tap to sprint"),
   N_("Pushing the left stick twice in a row holds the sprint button.")},
  {S_SPRINT_TIME, CAT_CONTROLS, ROW_CHOICE, N_("Double tap delay"),
   N_("Longest time between the two pushes. X: type a value in milliseconds.")},
  {S_MOTION, CAT_CONTROLS, ROW_TOGGLE, N_("Motion sensors"),
   N_("Sends the gyroscope and the accelerometer to the PC.")},
  {S_MOTION_X, CAT_CONTROLS, ROW_CHOICE, N_("Motion sensitivity, horizontal"),
   N_("X: type a value. 1 is the normal sensitivity.")},
  {S_MOTION_Y, CAT_CONTROLS, ROW_CHOICE, N_("Motion sensitivity, vertical"),
   N_("X: type a value. 1 is the normal sensitivity.")},
  {S_KEYBOARD_LAYOUT, CAT_CONTROLS, ROW_CHOICE, N_("Keyboard layout"),
   N_("Text: characters arrive as typed, whatever the PC keyboard. Or the PC's layout, for games that read keys.")},

  /* Touch */
  {S_TOUCH_MODE, CAT_TOUCH, ROW_CHOICE, N_("Front touch screen"),
   N_("Trackpad: swipe moves the pointer, tap clicks. Or a PlayStation touch pad, direct pointing, a pen tablet (Sunshine).")},
  {S_MOUSE_ACCEL, CAT_TOUCH, ROW_CHOICE, N_("Pointer acceleration"),
   N_("Trackpad mode: the pointer travels further for the same swipe. 0: off.")},
  {S_SPECIAL_KEYS, CAT_TOUCH, ROW_TOGGLE, N_("Corner shortcuts"),
   N_("Touching a corner of the screen sends a key or runs an action.")},
  {S_SPECIAL_KEYS_SETUP, CAT_TOUCH, ROW_ACTION, N_("Set up the corner shortcuts"),
   N_("Size and position of the corners, and what each one does.")},
  {S_DEADZONE, CAT_TOUCH, ROW_ACTION, N_("Rear touch pad dead zone"),
   N_("Part of the rear touch pad that is ignored, to avoid accidental presses.")},

  /* Advanced */
  {S_REMOTE, CAT_ADVANCED, ROW_TOGGLE, N_("Optimise for the Internet"),
   N_("For playing away from home. Leave off on your home network.")},
  {S_RFI, CAT_ADVANCED, ROW_TOGGLE, N_("Reference frame invalidation"),
   N_("Recovers from lost frames without a key frame. NVIDIA hosts only: leave off otherwise.")},
  {S_SOPS, CAT_ADVANCED, ROW_TOGGLE, N_("Let the PC adapt its screen"),
   N_("Sunshine can switch the PC screen to the stream's resolution (Display device options).")},
  {S_FRAME_PACER, CAT_ADVANCED, ROW_TOGGLE, N_("Frame pacer"),
   N_("Original frame pacing. Only used by the Direct display.")},
  {S_VBLANK, CAT_ADVANCED, ROW_TOGGLE, N_("Wait for the screen refresh"),
   N_("Direct display only: less tearing, a little more delay.")},
  {S_PINNING, CAT_ADVANCED, ROW_TOGGLE, N_("Spread work over the cores"),
   N_("Network, decoding and display each on their own core. Off: the system places them.")},
  {S_KEEPALIVE, CAT_ADVANCED, ROW_TOGGLE, N_("Keep the link to the PC open"),
   N_("Experimental: the menus and covers load faster. Turn off if the PC stops answering.")},
  {S_MSAA, CAT_ADVANCED, ROW_TOGGLE, N_("Smooth edges"),
   N_("4x anti-aliasing of the menus and the stream. Restart VitaMoon to apply a change.")},
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

/* ---- values ---- */

static const char *k_controller_names[] = {"Xbox", "PlayStation"};

/* By DISPLAY_MODE_* value; offered in the order of k_display_order */
static const char *k_display_names[] = {N_("Direct (original)"), N_("Low latency"), N_("Smooth"), N_("Paced")};
static const int k_display_order[] = {DISPLAY_MODE_SMOOTH, DISPLAY_MODE_PACED, DISPLAY_MODE_LOW_LATENCY,
                                      DISPLAY_MODE_DIRECT};
static const int k_packet_values[] = {1024, 1200, 1392};
static const int k_reorder_values[] = {0, 10, 20, 30};
/* 0 is the original behaviour: the screen works like a laptop trackpad */
static const char *k_touch_names[] = {N_("Trackpad"), N_("PS touch pad"), N_("Direct pointing"), N_("Pen tablet")};
static const char *k_keyboard_names[] = {N_("English (US)"), N_("Spanish (Spain)"), N_("Spanish (Latin America)"),
                                          N_("Text, any PC layout")};

static const int k_fps_values[] = {24, 30, 40, 50, 60};
/* kbps; 0 stands for the automatic bitrate */
static const int k_bitrate_values[] = {0, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 12000, 15000, 20000};
static const int k_sprint_values[] = {150, 200, 250, 300, 400, 500};

/* Profiles set the display, sound, decoder catch-up, colour decoding, wait for
 * late packets and core placement at once. Original turns all of them back to
 * the app's original behaviour. */
enum { PROFILE_SMOOTH, PROFILE_PACED, PROFILE_LATENCY, PROFILE_ORIGINAL, PROFILE_CUSTOM };
#define PROFILE_PRESETS 4
static const char *k_profile_names[] = {N_("Smooth"), N_("Paced"), N_("Lowest latency"), N_("Original"), N_("Custom")};
static const int k_profile_display[] = {DISPLAY_MODE_SMOOTH, DISPLAY_MODE_PACED, DISPLAY_MODE_LOW_LATENCY,
                                        DISPLAY_MODE_DIRECT};
#define REORDER_DEFAULT_MS 20

static int current_profile(void) {
  for (int p = 0; p < PROFILE_PRESETS; p++) {
    bool on = p != PROFILE_ORIGINAL;
    if (config.display_mode == k_profile_display[p] && config.low_latency_audio == on &&
        config.decoder_catchup == on && config.decode_yuv == on && config.thread_pinning == on &&
        config.video_reorder_wait_ms == (on ? REORDER_DEFAULT_MS : 0))
      return p;
  }
  return PROFILE_CUSTOM;
}

static void apply_profile(int profile) {
  bool on = profile != PROFILE_ORIGINAL;
  config.display_mode = k_profile_display[profile];
  config.low_latency_audio = on;
  config.decoder_catchup = on;
  config.decode_yuv = on;
  config.thread_pinning = on;
  config.video_reorder_wait_ms = on ? REORDER_DEFAULT_MS : 0;
}

/* Resolutions the hardware decoder accepts, probed once. */
static void probe_resolutions(void) {
  if (support_resolution_count || vitavideo_initialized())
    return;
  for (int i = 0; i < MAX_RESOLUTION; i++) {
    SceVideodecQueryInitInfoHwAvcdec dec;
    dec.size = sizeof(SceVideodecQueryInitInfoHwAvcdec);
    dec.horizontal = VITA_DECODER_RESOLUTION(RESOLUTIONS[i][0]);
    dec.vertical = VITA_DECODER_RESOLUTION(RESOLUTIONS[i][1]);
    dec.numOfRefFrames = 5;
    dec.numOfStreams = 1;
    int ret = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, &dec);
    sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
    if (ret < 0)
      continue; // unsupported resolution
    support_resolution_idx[support_resolution_count++] = i;
  }
}

static int resolution_position(void) {
  for (int i = 0; i < support_resolution_count; i++) {
    const int *r = RESOLUTIONS[support_resolution_idx[i]];
    if (r[0] == config.stream.width && r[1] == config.stream.height)
      return i;
  }
  return -1;
}

/* Index of `value` in a sorted list, or of the nearest entry in direction dir. */
static int step_in_list(const int *list, int count, int value, int dir) {
  int pos = 0;
  if (dir > 0) {
    for (pos = 0; pos < count && list[pos] <= value; pos++) ;
    return pos >= count ? count - 1 : pos;
  }
  for (pos = count - 1; pos >= 0 && list[pos] >= value; pos--) ;
  return pos < 0 ? 0 : pos;
}

static void format_mbps(int kbps, char *out, size_t n) {
  char number[16];
  if (kbps % 1000 == 0)
    snprintf(number, sizeof(number), "%d", kbps / 1000);
  else
    snprintf(number, sizeof(number), "%d.%d", kbps / 1000, (kbps % 1000) / 100);
  snprintf(out, n, tr("%s Mbps"), number);
}

static bool row_enabled(int id) {
  switch (id) {
    case S_RESOLUTION: return !vitavideo_initialized() && support_resolution_count > 0;
    case S_SPRINT_TIME: return config.enable_double_tap_sprint;
    case S_MOTION_X:
    case S_MOTION_Y: return config.enable_motion_controls;
    case S_MOUSE_ACCEL: return config.touchscreen_mode == 0;
    case S_SPECIAL_KEYS_SETUP: return config.enable_front_touchzones;
    case S_FRAME_PACER:
    case S_VBLANK: return config.display_mode == DISPLAY_MODE_DIRECT;
    default: return true;
  }
}

static bool row_is_on(int id) {
  switch (id) {
    case S_SHOW_STATS: return config.show_fps;
    case S_SWAP_XO: return config.jp_layout;
    case S_KEEP_AWAKE: return config.disable_powersave;
    case S_AUTO_RECONNECT: return config.auto_reconnect;
    case S_DEBUG_LOG: return config.save_debug_log;
    case S_CROP: return config.center_region_only;
    case S_LOCAL_AUDIO: return config.localaudio;
    case S_LL_AUDIO: return config.low_latency_audio;
    case S_CATCHUP: return config.decoder_catchup;
    case S_BOOST: return config.boost_clocks;
    case S_MATCH_REFRESH: return config.match_refresh_rate;
    case S_SWAP_SHOULDER: return config.swap_shoulder_buttons;
    case S_MAPPING: return config.mapping != NULL;
    case S_PS_CAPTURE: return config.enable_psbutton_capture;
    case S_DOUBLE_TAP_SPRINT: return config.enable_double_tap_sprint;
    case S_MOTION: return config.enable_motion_controls;
    case S_SPECIAL_KEYS: return config.enable_front_touchzones;
    case S_REMOTE: return config.stream.streamingRemotely != 0;
    case S_RFI: return config.enable_ref_frame_invalidation;
    case S_SOPS: return config.sops;
    case S_FRAME_PACER: return config.enable_frame_pacer;
    case S_VBLANK: return config.enable_vita_vblank_wait;
    case S_KEEPALIVE: return config.http_keepalive;
    case S_MSAA: return config.ui_msaa;
    case S_YUV: return config.decode_yuv;
    case S_PINNING: return config.thread_pinning;
    case S_INPUT_SYNC: return config.input_sync;
    default: return false;
  }
}

static void row_value(int id, char *out, size_t n) {
  out[0] = 0;
  switch (id) {
    case S_LANGUAGE:
      snprintf(out, n, "%s", i18n_language_name(i18n_language()));
      break;
    case S_THEME:
      snprintf(out, n, "%s", tr(theme_name(config.theme)));
      break;
    case S_RESOLUTION:
      snprintf(out, n, "%d x %d", config.stream.width, config.stream.height);
      break;
    case S_FPS:
      snprintf(out, n, tr("%d fps"), config.stream.fps);
      break;
    case S_BITRATE: {
      char mbps[32];
      format_mbps(config.stream.bitrate, mbps, sizeof(mbps));
      if (config.auto_bitrate)
        snprintf(out, n, tr("Auto (%s)"), mbps);
      else
        snprintf(out, n, "%s", mbps);
      break;
    }
    case S_PROFILE:
      snprintf(out, n, "%s", tr(k_profile_names[current_profile()]));
      break;
    case S_DISPLAY_MODE:
      snprintf(out, n, "%s", tr(k_display_names[config.display_mode >= DISPLAY_MODE_DIRECT &&
                                                  config.display_mode <= DISPLAY_MODE_PACED ? config.display_mode : 0]));
      break;
    case S_REORDER:
      if (config.video_reorder_wait_ms <= 0)
        snprintf(out, n, "%s", tr("No wait"));
      else
        snprintf(out, n, "%d ms", config.video_reorder_wait_ms);
      break;
    case S_PACKET_SIZE:
      snprintf(out, n, tr("%d bytes"), config.stream.packetSize);
      break;
    case S_CONTROLLER_TYPE:
      snprintf(out, n, "%s", k_controller_names[config.controller_type == 1 ? 0 : 1]);
      break;
    case S_SPRINT_TIME:
      snprintf(out, n, "%u ms", config.double_tap_sprint_step_time);
      break;
    case S_MOTION_X:
      snprintf(out, n, "%.2f", config.motion_controls_scalar_x);
      break;
    case S_MOTION_Y:
      snprintf(out, n, "%.2f", config.motion_controls_scalar_y);
      break;
    case S_KEYBOARD_LAYOUT:
      snprintf(out, n, "%s", tr(k_keyboard_names[config.keyboard_layout >= 0 && config.keyboard_layout < KB_LAYOUT_COUNT ? config.keyboard_layout : 0]));
      break;
    case S_TOUCH_MODE:
      snprintf(out, n, "%s", tr(k_touch_names[config.touchscreen_mode >= 0 && config.touchscreen_mode <= 3 ? config.touchscreen_mode : 0]));
      break;
    case S_MOUSE_ACCEL:
      snprintf(out, n, "%d", config.mouse_acceleration);
      break;
  }
}

static bool ask_number(const char *title, char *value, size_t n) {
  char text[512] = "";
  if (ime_dialog_number(text, title, "") != 0)
    return false;
  snprintf(value, n, "%s", text);
  return true;
}

/* dir: -1 = left, +1 = right, 0 = confirm (X). Returns true if something changed. */
static bool row_change(int id, int dir) {
  if (!row_enabled(id))
    return false;
  int step = dir < 0 ? -1 : 1;   /* confirm moves forward */
  char typed[64];

  switch (id) {
    /* ---- toggles ---- */
    case S_SHOW_STATS: config.show_fps = !config.show_fps; return true;
    case S_SWAP_XO:
      config.jp_layout = !config.jp_layout;
      update_layout();
      return true;
    case S_KEEP_AWAKE: config.disable_powersave = !config.disable_powersave; return true;
    case S_AUTO_RECONNECT: config.auto_reconnect = !config.auto_reconnect; return true;
    case S_DEBUG_LOG:
      config.save_debug_log = !config.save_debug_log;
      log_set_detailed(config.save_debug_log);
      LOGI("app", "detailed log %s", config.save_debug_log ? "on" : "off");
      return true;
    case S_CROP: config.center_region_only = !config.center_region_only; return true;
    case S_LOCAL_AUDIO: config.localaudio = !config.localaudio; return true;
    case S_LL_AUDIO: config.low_latency_audio = !config.low_latency_audio; return true;
    case S_CATCHUP: config.decoder_catchup = !config.decoder_catchup; return true;
    case S_BOOST: config.boost_clocks = !config.boost_clocks; return true;
    case S_MATCH_REFRESH: config.match_refresh_rate = !config.match_refresh_rate; return true;
    case S_PS_CAPTURE: config.enable_psbutton_capture = !config.enable_psbutton_capture; return true;
    case S_DOUBLE_TAP_SPRINT: config.enable_double_tap_sprint = !config.enable_double_tap_sprint; return true;
    case S_MOTION: config.enable_motion_controls = !config.enable_motion_controls; return true;
    case S_SPECIAL_KEYS: config.enable_front_touchzones = !config.enable_front_touchzones; return true;
    case S_REMOTE: config.stream.streamingRemotely = config.stream.streamingRemotely ? 0 : 1; return true;
    case S_RFI: config.enable_ref_frame_invalidation = !config.enable_ref_frame_invalidation; return true;
    case S_SOPS: config.sops = !config.sops; return true;
    case S_FRAME_PACER: config.enable_frame_pacer = !config.enable_frame_pacer; return true;
    case S_VBLANK: config.enable_vita_vblank_wait = !config.enable_vita_vblank_wait; return true;
    case S_KEEPALIVE: config.http_keepalive = !config.http_keepalive; return true;
    case S_MSAA: config.ui_msaa = !config.ui_msaa; return true;
    case S_YUV: config.decode_yuv = !config.decode_yuv; return true;
    case S_PINNING: config.thread_pinning = !config.thread_pinning; return true;
    case S_INPUT_SYNC: config.input_sync = !config.input_sync; return true;
    case S_SWAP_SHOULDER:
      config.swap_shoulder_buttons = !config.swap_shoulder_buttons;
      // Mutually exclusive: the swap turns the mapping file off
      if (config.swap_shoulder_buttons && config.mapping) {
        free(config.mapping);
        config.mapping = NULL;
      }
      return true;
    case S_MAPPING:
      if (config.mapping) {
        free(config.mapping);
        config.mapping = NULL;
      } else {
        config.mapping = strdup("mappings/vita.conf");
        config.swap_shoulder_buttons = false;
      }
      return true;

    /* ---- choices ---- */
    case S_LANGUAGE:
      config.language = (i18n_language() + LANG_COUNT + step) % LANG_COUNT;
      i18n_set_language(config.language);
      return true;
    case S_THEME:
      config.theme = (config.theme + THEME_COUNT + (step ? step : 1)) % THEME_COUNT;
      theme_select(config.theme);
      return true;
    case S_RESOLUTION: {
      int pos = resolution_position();
      pos = pos < 0 ? 0 : (pos + support_resolution_count + step) % support_resolution_count;
      config.stream.width = RESOLUTIONS[support_resolution_idx[pos]][0];
      config.stream.height = RESOLUTIONS[support_resolution_idx[pos]][1];
      config_apply_auto_bitrate(&config);
      return true;
    }
    case S_FPS: {
      const int count = sizeof(k_fps_values) / sizeof(int);
      int pos = step_in_list(k_fps_values, count, config.stream.fps, step);
      config.stream.fps = k_fps_values[pos];
      config_apply_auto_bitrate(&config);
      return true;
    }
    case S_BITRATE: {
      if (dir == 0) {
        if (!ask_number(tr("Bitrate in kbps (for example 8000)"), typed, sizeof(typed)))
          return false;
        int kbps = atoi(typed);
        if (kbps < 500 || kbps > 100000) {
          display_error(tr("Incorrect bitrate entered: %s"), typed);
          return false;
        }
        config.stream.bitrate = kbps;
        config.auto_bitrate = false;
        return true;
      }
      const int count = sizeof(k_bitrate_values) / sizeof(int);
      int pos = step_in_list(k_bitrate_values, count, config.auto_bitrate ? 0 : config.stream.bitrate, step);
      config.auto_bitrate = k_bitrate_values[pos] == 0;
      if (config.auto_bitrate)
        config_apply_auto_bitrate(&config);
      else
        config.stream.bitrate = k_bitrate_values[pos];
      return true;
    }
    case S_PROFILE: {
      int profile = current_profile();
      // Custom is not a stop of its own: leaving it goes to the nearest preset
      if (profile == PROFILE_CUSTOM)
        profile = step > 0 ? PROFILE_SMOOTH : PROFILE_ORIGINAL;
      else
        profile = (profile + PROFILE_PRESETS + step) % PROFILE_PRESETS;
      apply_profile(profile);
      return true;
    }
    case S_DISPLAY_MODE: {
      // offered from the default (Smooth) down to the original (Direct)
      const int count = sizeof(k_display_order) / sizeof(int);
      int pos = 0;
      while (pos < count && k_display_order[pos] != config.display_mode)
        pos++;
      if (pos >= count)
        pos = 0;
      config.display_mode = k_display_order[(pos + count + step) % count];
      return true;
    }
    case S_REORDER: {
      const int count = sizeof(k_reorder_values) / sizeof(int);
      int pos = step_in_list(k_reorder_values, count, config.video_reorder_wait_ms, step);
      if (dir == 0)  // X: next value, round
        pos = config.video_reorder_wait_ms >= k_reorder_values[count - 1] ? 0
              : step_in_list(k_reorder_values, count, config.video_reorder_wait_ms, 1);
      config.video_reorder_wait_ms = k_reorder_values[pos];
      return true;
    }
    case S_PACKET_SIZE: {
      // 1024 is Moonlight's value; 1392 is the largest multiple of 16 that fits
      // a 1500-byte frame with its headers: fewer, larger packets per frame.
      const int count = sizeof(k_packet_values) / sizeof(int);
      int pos;
      if (dir == 0)
        pos = config.stream.packetSize >= k_packet_values[count - 1] ? 0
              : step_in_list(k_packet_values, count, config.stream.packetSize, 1);
      else
        pos = step_in_list(k_packet_values, count, config.stream.packetSize, step);
      config.stream.packetSize = k_packet_values[pos];
      return true;
    }
    case S_CONTROLLER_TYPE:
      config.controller_type = config.controller_type == 1 ? 2 : 1;
      return true;
    case S_SPRINT_TIME: {
      if (dir == 0) {
        if (!ask_number(tr("Enter Sprint Step Time in Milliseconds"), typed, sizeof(typed)))
          return false;
        int ms = atoi(typed);
        if (ms < 50 || ms > 2000) {
          display_error(tr("Incorrect step time entered: %s"), typed);
          return false;
        }
        config.double_tap_sprint_step_time = ms;
        return true;
      }
      const int count = sizeof(k_sprint_values) / sizeof(int);
      int pos = step_in_list(k_sprint_values, count, (int)config.double_tap_sprint_step_time, step);
      config.double_tap_sprint_step_time = k_sprint_values[pos];
      return true;
    }
    case S_MOTION_X:
    case S_MOTION_Y: {
      float *scalar = id == S_MOTION_X ? &config.motion_controls_scalar_x : &config.motion_controls_scalar_y;
      if (dir == 0) {
        if (!ask_number(tr("Sensitivity (1 = normal)"), typed, sizeof(typed)))
          return false;
        float value = atof(typed);
        if (value <= 0.0f || value > 20.0f) {
          display_error(tr("Incorrect scalar entered: %s"), typed);
          return false;
        }
        *scalar = value;
        return true;
      }
      float value = *scalar + step * 0.25f;
      if (value < 0.25f) value = 0.25f;
      if (value > 10.0f) value = 10.0f;
      *scalar = value;
      return true;
    }
    case S_KEYBOARD_LAYOUT:
      config.keyboard_layout = (config.keyboard_layout + KB_LAYOUT_COUNT + step) % KB_LAYOUT_COUNT;
      keyboardsystem_set_layout((KeyboardLayout)config.keyboard_layout);
      return true;
    case S_TOUCH_MODE:
      config.touchscreen_mode = (config.touchscreen_mode + 4 + step) % 4;
      // touchabsolute only in the direct pointing mode
      touchabsolute_enable(config.touchscreen_mode == 2);
      return true;
    case S_MOUSE_ACCEL: {
      int value = config.mouse_acceleration + step * 15;
      if (value < 0) value = 0;
      if (value > 300) value = 300;
      if (value == config.mouse_acceleration)
        return false;
      config.mouse_acceleration = value;
      return true;
    }

    /* ---- actions ---- */
    case S_SPECIAL_KEYS_SETUP:
      if (dir == 0) {
        special_keys_menu();
        return true;
      }
      return false;
    case S_DEADZONE:
      if (dir == 0) {
        deadzone_settings_menu();
        return true;
      }
      return false;
  }
  return false;
}

/* Why a greyed-out row cannot be changed. */
static const char *row_blocked_reason(int id) {
  switch (id) {
    case S_RESOLUTION: return tr("Cannot be changed while a stream is paused.");
    case S_SPRINT_TIME: return tr("Turn on Double tap to sprint first.");
    case S_MOTION_X:
    case S_MOTION_Y: return tr("Turn on Motion sensors first.");
    case S_MOUSE_ACCEL: return tr("Only used when the front touch screen is a trackpad.");
    case S_SPECIAL_KEYS_SETUP: return tr("Turn on Corner shortcuts first.");
    case S_FRAME_PACER:
    case S_VBLANK: return tr("Only used by the Direct display.");
    default: return "";
  }
}

/* ---- layout (see theme.h for the grid) ---- */
#define SET_CAT_X      TH_MARGIN
#define SET_CAT_W      236
#define SET_PANE_X     (SET_CAT_X + SET_CAT_W + 28)     /* 312 */
#define SET_PANE_W     (TH_RIGHT - SET_PANE_X)          /* 600 */
#define SET_ROW_H      44
#define SET_TOP        TH_CONTENT_Y
#define SET_VISIBLE    7                                /* rows shown without scrolling */
#define SET_BOTTOM     (SET_TOP + SET_VISIBLE * SET_ROW_H)
#define SET_HELP_Y     (SET_BOTTOM + 26)
#define SET_VALUE_ZONE 248

typedef struct {
  int category;
  int row;              /* index among the rows of the category */
  bool in_rows;         /* focus: false = categories, true = rows */
  float cat_focus_y, row_focus_y, scroll, target_scroll;
  float pane_alpha;     /* fades the right pane in after a category change */
  float knob[ROW_COUNT];
  bool first_frame;
} settings_view;

static int category_rows(int category, int out[ROW_COUNT]) {
  int n = 0;
  for (int i = 0; i < ROW_COUNT; i++)
    if (k_rows[i].category == category)
      out[n++] = i;
  return n;
}

static void settings_draw_screen(settings_view *v) {
  int rows[ROW_COUNT];
  int count = category_rows(v->category, rows);

  theme_topbar(NULL);
  theme_header(tr("Settings"));

  /* ---- left pane: categories ---- */
  theme_focus(SET_CAT_X, v->cat_focus_y + 2, SET_CAT_W, SET_ROW_H - 4);
  for (int c = 0; c < CAT_COUNT; c++) {
    float y = SET_TOP + c * SET_ROW_H;
    bool current = c == v->category;
    unsigned int color = current ? TH_TEXT : (v->in_rows ? TH_TEXT3 : TH_TEXT2);
    theme_text_fit(SET_CAT_X + 18, y + SET_ROW_H / 2.0f + TS_BODY * 0.36f, color, TS_BODY,
                   tr(k_categories[c]), SET_CAT_W - 30);
  }
  /* divider between the panes */
  theme_rect(SET_PANE_X - 14, SET_TOP + 4, 1, SET_VISIBLE * SET_ROW_H - 8, TH_DIVIDER);

  /* ---- right pane: rows of the category ---- */
  theme_set_alpha(v->pane_alpha);
  bool scrollable = count > SET_VISIBLE;
  if (scrollable) {
    vita2d_set_clip_rectangle(SET_PANE_X, SET_TOP, WIDTH, SET_BOTTOM);
    vita2d_enable_clipping();
  }
  if (v->in_rows)
    theme_focus(SET_PANE_X, v->row_focus_y - v->scroll + 2, SET_PANE_W, SET_ROW_H - 4);

  const float right = TH_RIGHT - 16;
  for (int i = 0; i < count; i++) {
    const setting_row *row = &k_rows[rows[i]];
    float y = SET_TOP + i * SET_ROW_H - v->scroll;
    if (y + SET_ROW_H < SET_TOP || y > SET_BOTTOM)
      continue;
    float cy = y + SET_ROW_H / 2.0f;
    bool focused = v->in_rows && i == v->row;
    bool enabled = row_enabled(row->id);
    unsigned int label_color = !enabled ? TH_TEXT_OFF : (focused ? TH_TEXT : TH_TEXT2);
    unsigned int value_color = !enabled ? TH_TEXT_OFF : (focused ? TH_TEXT : TH_TEXT3);
    float label_right = right;

    if (!focused)
      theme_rect(SET_PANE_X, y + SET_ROW_H - 1, SET_PANE_W, 1, TH_DIVIDER);

    if (row->type == ROW_TOGGLE) {
      /* switch on the right edge, its state written at a fixed place before it */
      bool on = row_is_on(row->id);
      v->knob[rows[i]] = v->first_frame ? (on ? 1.0f : 0.0f)
                                        : v->knob[rows[i]] + ((on ? 1.0f : 0.0f) - v->knob[rows[i]]) * 0.4f;
      if (!enabled) theme_set_alpha(v->pane_alpha * 0.45f);
      theme_toggle(right - 44, cy, v->knob[rows[i]]);
      if (!enabled) theme_set_alpha(v->pane_alpha);
      theme_text_right(right - 56, cy + TS_SMALL * 0.36f, value_color, TS_SMALL, on ? tr("On") : tr("Off"));
      label_right = right - 56 - 40;
    } else if (row->type == ROW_CHOICE) {
      /* "< value >" in a fixed zone, the value centred between the arrows */
      char value[96];
      row_value(row->id, value, sizeof(value));
      unsigned int chev = !enabled ? RGBA8(255, 255, 255, 40) : (focused ? TH_TEXT : RGBA8(255, 255, 255, 90));
      theme_chevron(right - SET_VALUE_ZONE + 6, cy, 5.5f, -1, chev);
      theme_chevron(right - 6, cy, 5.5f, 1, chev);
      theme_text_fit_center(right - SET_VALUE_ZONE / 2.0f, cy + TS_HINT * 0.36f, value_color, TS_HINT,
                            value, SET_VALUE_ZONE - 40);
      label_right = right - SET_VALUE_ZONE - 12;
    } else {
      theme_chevron(right - 6, cy, 5.5f, 1, !enabled ? RGBA8(255, 255, 255, 40) : (focused ? TH_TEXT : RGBA8(255, 255, 255, 90)));
      label_right = right - 28;
    }
    theme_text_fit(SET_PANE_X + 18, cy + TS_BODY * 0.36f, label_color, TS_BODY, tr(row->label),
                   (int)(label_right - (SET_PANE_X + 18)));
  }
  if (scrollable) {
    vita2d_disable_clipping();
    float track_h = SET_VISIBLE * SET_ROW_H - 8;
    float bar_h = track_h * SET_VISIBLE / count;
    float max_scroll = (count - SET_VISIBLE) * SET_ROW_H;
    theme_rounded_rect(TH_RIGHT + 8, SET_TOP + 4 + (track_h - bar_h) * v->scroll / max_scroll, 3, bar_h, 1.5f,
                       RGBA8(255, 255, 255, 70));
  }

  /* ---- description of the focused row (two lines at most) ---- */
  if (v->in_rows && v->row < count) {
    const setting_row *row = &k_rows[rows[v->row]];
    char text[512];
    if (!row_enabled(row->id))
      snprintf(text, sizeof(text), "%s", row_blocked_reason(row->id));
    else
      snprintf(text, sizeof(text), "%s", tr(row->help));
    char lines[4][256];
    int n = theme_wrap(TS_SMALL, text, SET_PANE_W - 36, lines, 4);
#ifdef UI_PREVIEW
    if (n > 2) fprintf(stderr, "HELP TOO LONG (%d lines): %s\n", n, text);
#endif
    for (int i = 0; i < n && i < 2; i++)
      theme_text(SET_PANE_X + 18, SET_HELP_Y + i * 19, TH_TEXT2, TS_SMALL, lines[i]);
  }
  theme_set_alpha(1.0f);

  /* ---- hints ---- */
  if (!v->in_rows) {
    const theme_hint_item hints[] = {
      {theme_confirm_glyph(), tr("Open")},
      {theme_cancel_glyph(), tr("Back")},
      {GLYPH_LR, tr("Category")},
    };
    theme_hints(hints, 3);
  } else {
    const setting_row *row = &k_rows[rows[v->row < count ? v->row : 0]];
    const theme_hint_item hints[] = {
      {GLYPH_DPAD_LR, tr("Change")},
      {theme_confirm_glyph(), row->type == ROW_ACTION ? tr("Open") : (row->type == ROW_TOGGLE ? tr("Switch") : tr("Next"))},
      {theme_cancel_glyph(), tr("Back")},
      {GLYPH_LR, tr("Category")},
    };
    theme_hints(hints + (row->type == ROW_ACTION ? 1 : 0), row->type == ROW_ACTION ? 3 : 4);
  }
}

void ui_settings_save_config() {
  config_save(config_path, &config);
  vita_debug_log("Config saved to %s", config_path);
}

int ui_settings_menu() {
  ui_end();
  gui_touch_reset();
  probe_resolutions();

  static int last_category = CAT_GENERAL;   /* reopen where the user left */
  settings_view v = {0};
  v.category = last_category;
  v.first_frame = true;
  v.pane_alpha = 0.0f;

  while (true) {
    int rows[ROW_COUNT];
    int count = category_rows(v.category, rows);
    if (v.row >= count) v.row = count - 1;
    if (v.row < 0) v.row = 0;

    /* scrolling: keep the focused row and one row around it in view */
    float max_scroll = count > SET_VISIBLE ? (count - SET_VISIBLE) * SET_ROW_H : 0;
    if (!v.in_rows || v.row == 0) {
      v.target_scroll = 0;
    } else {
      float top = v.row * SET_ROW_H;
      if (top - SET_ROW_H < v.target_scroll)
        v.target_scroll = top - SET_ROW_H;
      else if (top + 2 * SET_ROW_H > v.target_scroll + SET_VISIBLE * SET_ROW_H)
        v.target_scroll = top + 2 * SET_ROW_H - SET_VISIBLE * SET_ROW_H;
    }
    if (v.target_scroll > max_scroll) v.target_scroll = max_scroll;
    if (v.target_scroll < 0) v.target_scroll = 0;
    float cat_y = SET_TOP + v.category * SET_ROW_H;
    float row_y = SET_TOP + v.row * SET_ROW_H;
    if (v.first_frame) {
      v.cat_focus_y = cat_y;
      v.row_focus_y = row_y;
      v.scroll = v.target_scroll;
    } else {
      v.cat_focus_y = theme_approach(v.cat_focus_y, cat_y);
      v.row_focus_y = theme_approach(v.row_focus_y, row_y);
      v.scroll = theme_approach(v.scroll, v.target_scroll);
    }
    v.pane_alpha = v.pane_alpha + 0.2f > 1.0f ? 1.0f : v.pane_alpha + 0.2f;

    ui_start();
    settings_draw_screen(&v);
    v.first_frame = false;

    int buttons = read_buttons();
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    bool held = buttons & SCE_CTRL_HOLD;
    bool confirm = (buttons & config.btn_confirm) && !held;
    bool cancel = (buttons & config.btn_cancel) && !held;
    int new_category = v.category;

    /* touch: a tap focuses; a tap on the focused row acts like X */
    int tx, ty;
    if (gui_touch_tap(&touch, &tx, &ty) && ty >= SET_TOP && ty < SET_BOTTOM) {
      if (tx >= SET_CAT_X && tx < SET_CAT_X + SET_CAT_W) {
        int c = (ty - SET_TOP) / SET_ROW_H;
        if (c >= 0 && c < CAT_COUNT) {
          new_category = c;
          v.in_rows = false;
        }
      } else if (tx >= SET_PANE_X) {
        int r = (int)((ty - SET_TOP + v.scroll) / SET_ROW_H);
        if (r >= 0 && r < count) {
          if (v.in_rows && r == v.row)
            confirm = true;
          v.in_rows = true;
          v.row = r;
        }
      }
    }

    if ((buttons & SCE_CTRL_LTRIGGER) && !held) new_category = (v.category + CAT_COUNT - 1) % CAT_COUNT;
    if ((buttons & SCE_CTRL_RTRIGGER) && !held) new_category = (v.category + 1) % CAT_COUNT;

    if (!v.in_rows) {
      if (buttons & SCE_CTRL_DOWN) new_category = v.category < CAT_COUNT - 1 ? v.category + 1 : v.category;
      if (buttons & SCE_CTRL_UP) new_category = v.category > 0 ? v.category - 1 : v.category;
      if (confirm || ((buttons & SCE_CTRL_RIGHT) && !held)) {
        v.in_rows = count > 0;
      } else if (cancel) {
        break;
      }
    } else {
      const setting_row *row = &k_rows[rows[v.row]];
      if (buttons & SCE_CTRL_DOWN) v.row = v.row < count - 1 ? v.row + 1 : v.row;
      if (buttons & SCE_CTRL_UP) v.row = v.row > 0 ? v.row - 1 : v.row;
      bool changed = false;
      if (buttons & SCE_CTRL_LEFT) {
        if (row->type == ROW_ACTION && !held)
          v.in_rows = false;
        else if (row->type == ROW_TOGGLE)
          changed = !held && row_is_on(row->id) && row_change(row->id, -1);
        else if (row->type == ROW_CHOICE)
          changed = row_change(row->id, -1);
      } else if (buttons & SCE_CTRL_RIGHT) {
        if (row->type == ROW_TOGGLE)
          changed = !held && !row_is_on(row->id) && row_change(row->id, 1);
        else if (row->type == ROW_CHOICE)
          changed = row_change(row->id, 1);
      } else if (confirm) {
        changed = row_change(row->id, 0);
      } else if (cancel) {
        v.in_rows = false;
      }
      if (changed && (row->id == S_LANGUAGE || row->id == S_THEME))
        ui_settings_save_config();   /* kept even if the app is closed from here */
    }

    if (new_category != v.category) {
      v.category = new_category;
      v.row = 0;
      v.scroll = 0;
      v.target_scroll = 0;
      v.pane_alpha = 0.0f;
      v.row_focus_y = SET_TOP;
    }

    ui_end();
  }

  ui_end();
  last_category = v.category;
  ui_settings_save_config();
  update_layout();
  return 1;
}
