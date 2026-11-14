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
#include "ui_keyboard.h"

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
// Stored in English; translated with tr() at display time. NULL-terminated
// because ui_hotkey.c walks the array until the sentinel.
char *settings_special_names[] = {
  "None",
  // special
  "Special inputs",
  "Pause stream",
  "Open keyboard",
  // gamepad
  "Gamepad buttons",
  "Special (XBox button)",
  "LB", "RB", "LS", "RS", "LT", "RT",
  // mouse
  "Mouse buttons",
  "Left",
  "Right",
  "Middle(wheel)",
  "X1(4th)",
  "X2(5th)",
  // keyboard
  "Keyboard input codes",
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

static bool settings_loop_setup = 1;

/*
 * Deadzone
 */

#define lerp(value, from_max, to_max) ((((value*10) * (to_max*10))/(from_max*10))/10)
static void deadzone_draw() {
  int vertical = (WIDTH - config.back_deadzone.left - config.back_deadzone.right) / 2 + config.back_deadzone.left,
      horizontal = (HEIGHT - config.back_deadzone.top - config.back_deadzone.bottom) / 2 + config.back_deadzone.top;

  vita2d_draw_rectangle(
      config.back_deadzone.left,
      config.back_deadzone.top,
      WIDTH - config.back_deadzone.right - config.back_deadzone.left,
      HEIGHT - config.back_deadzone.bottom - config.back_deadzone.top,
      0x3000ff00
      );

  vita2d_draw_line(vertical, config.back_deadzone.top, vertical, HEIGHT - config.back_deadzone.bottom, 0xffffffff);
  vita2d_draw_line(config.back_deadzone.left, horizontal, WIDTH - config.back_deadzone.right, horizontal, 0xffffffff);

  SceTouchData touch_data;
  sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch_data, 1);

  for (int i = 0; i < touch_data.reportNum; i++) {
    int x = lerp(touch_data.report[i].x, 1919, 960);
    int y = lerp(touch_data.report[i].y, 1087, 544);
    if (x < config.back_deadzone.left || x > WIDTH - config.back_deadzone.right)
      continue;

    if (y < config.back_deadzone.top || y > HEIGHT - config.back_deadzone.bottom)
      continue;

    vita2d_draw_fill_circle(x, y, 30, 0xffffffff);
  }
}

static int deadzone_loop(int cursor, void *context, const input_data *input) {
  menu_entry *menu = context;

  bool left = input->buttons & SCE_CTRL_LEFT;
  bool right = input->buttons & SCE_CTRL_RIGHT;

  int delta = left ? -15 : (right ? 15 : 0);
  switch (cursor) {
    case 0: config.back_deadzone.top += delta; break;
    case 1: config.back_deadzone.right += delta; break;
    case 2: config.back_deadzone.bottom += delta; break;
    case 3: config.back_deadzone.left += delta; break;
  }

  settings_loop_setup = 0;
  char current[256];

  int numbers[] = {
    config.back_deadzone.top,
    config.back_deadzone.right,
    config.back_deadzone.bottom,
    config.back_deadzone.left
  };
  for (int i = 0; i < 4; i++) {
    sprintf(current, "%dpx", numbers[i]);
    strcpy(menu[i].subname, current);
  }

  return 0;
}

static int deadzone_settings_menu() {
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

  menu_entry menu[16];
  int idx = 0;
  menu[idx++] = (menu_entry) { .name = tr("Top: "), .disabled = false, .id = 0, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = tr("Left: "), .disabled = false, .id = 1, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = tr("Bottom: "), .disabled = false, .id = 2, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = tr("Right: "), .disabled = false, .id = 3, .suffix = ICON_LEFT_RIGHT_ARROWS };

  menu_geom geom = make_geom_centered(250, 120);
  geom.x = 50;
  geom.y = 50;
  geom.el = 25;
  return display_menu(menu, idx, &geom, &deadzone_loop, NULL, &deadzone_draw, &menu);
}

/*
 * Special keys
 */

static int special_keys_ord(char *text) {
  bool did_find = false;
  int i = 0;
  for (; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    if (strcmp(settings_special_names[i], text) == 0) {
      did_find = true;
      break;
    }
  }

  if (did_find) {
    return settings_special_codes[i];
  } else {
    return 0;
  }
}

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
      .disabled = (id >= INPUT_TYPE_DEF_NAME)
    };
    special_keys_name(id, menu[idx-1].name);
  }

  menu[idx++] = (menu_entry) { .name = "", .disabled = true, .separator = true };
  menu[idx++] = (menu_entry) { .name = tr("Enter manually ..."), .id = SETTINGS_SELECT_SPECIAL_KEY_MANUAL };

  int return_code = display_menu(menu, idx, NULL, &select_special_key_loop, NULL, NULL, code);
  for (int i = 0; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    free(menu[i].name);
  }

  return return_code;
}

static int special_keys_loop(int id, void *context, const input_data *input) {
  bool left = input->buttons & SCE_CTRL_LEFT;
  bool right = input->buttons & SCE_CTRL_RIGHT;
  int selected_ord = -1;
  int delta = left ? -15 : (right ? 15 : 0);

  switch (id) {
    case SETTINGS_SPECIAL_KEYS_OFFSET:
      config.special_keys.offset += delta;
      break;
    case SETTINGS_SPECIAL_KEYS_SIZE:
      config.special_keys.size += delta;
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
  sprintf(menu[idx++].subname, "%d", config.special_keys.offset);
  sprintf(menu[idx++].subname, "%d", config.special_keys.size);

  idx++;
  special_keys_name(config.special_keys.nw, menu[idx++].subname);
  special_keys_name(config.special_keys.ne, menu[idx++].subname);
  special_keys_name(config.special_keys.sw, menu[idx++].subname);
  special_keys_name(config.special_keys.se, menu[idx++].subname);

  return 0;
}

static void special_keys_draw() {
  int special_offset = config.special_keys.offset,
      special_size = config.special_keys.size;

  unsigned int color = 0xff006000;

  for (int i = TOUCHSEC_SPECIAL_NW; i <= TOUCHSEC_SPECIAL_SE; i++) {
    switch (i) {
      case TOUCHSEC_SPECIAL_SW:
        vita2d_draw_rectangle(
            special_offset,
            HEIGHT - special_size - special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case TOUCHSEC_SPECIAL_SE:
        vita2d_draw_rectangle(
            WIDTH - special_size - special_offset,
            HEIGHT - special_size - special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case TOUCHSEC_SPECIAL_NW:
        vita2d_draw_rectangle(
            special_offset,
            special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case TOUCHSEC_SPECIAL_NE:
        vita2d_draw_rectangle(
            WIDTH - special_size - special_offset,
            special_offset,
            special_size,
            special_size,
            color);
    }
  }

}

static int special_keys_menu() {
  menu_entry menu[16];
  int idx = 0;

  menu[idx++] = (menu_entry) { .name = tr("Offset"), .id = SETTINGS_SPECIAL_KEYS_OFFSET };
  menu[idx++] = (menu_entry) { .name = tr("Size"), .id = SETTINGS_SPECIAL_KEYS_SIZE };
  menu[idx++] = (menu_entry) { .name = tr("Assignments"), .disabled = true, .separator = false };
  menu[idx++] = (menu_entry) { .name = tr("Top left"), .id = TOUCHSEC_SPECIAL_NW };
  menu[idx++] = (menu_entry) { .name = tr("Top right"), .id = TOUCHSEC_SPECIAL_NE };
  menu[idx++] = (menu_entry) { .name = tr("Bottom left"), .id = TOUCHSEC_SPECIAL_SW };
  menu[idx++] = (menu_entry) { .name = tr("Bottom right"), .id = TOUCHSEC_SPECIAL_SE };

  return display_menu(menu, idx, NULL, &special_keys_loop, NULL, &special_keys_draw, &menu);
}



// --- Controller Type Selection ---
static const char* controller_type_names[] = {"Xbox", "PlayStation"};
static int controller_type_values[] = {1, 2}; // 1: Xbox, 2: PS
#define CONTROLLER_TYPE_COUNT 2

static int get_controller_type_index(int value) {
  for (int i = 0; i < CONTROLLER_TYPE_COUNT; ++i) {
    if (controller_type_values[i] == value) return i;
  }
  return 1; // Default to PlayStation if not found
}

/*
 * Main menu
 */

enum {
  SETTINGS_RESOLUTION = 100,
  SETTINGS_FPS,
  SETTINGS_BITRATE,
  SETTINGS_SOPS,
  SETTINGS_ENABLE_FRAME_INVAL,
  SETTINGS_ENABLE_STREAM_OPTIMIZE,
  SETTINGS_ENABLE_VITA_VBLANK_WAIT,
  SETTINGS_ENABLE_MOTION_CONTROLS, //Metalface
  SETTINGS_MOTION_CONTROLS_SCALAR_X,
  SETTINGS_MOTION_CONTROLS_SCALAR_Y,
  SETTINGS_ENABLE_DOUBLE_TAP_SPRINT, //Metalface
  SETTINGS_DOUBLE_TAP_SPRINT_STEP_TIME,
  SETTINGS_SAVE_DEBUG_LOG,
  SETTINGS_DISABLE_POWERSAVE,
  SETTINGS_JP_LAYOUT,
  SETTINGS_SHOW_FPS,
  SETTINGS_LOCAL_AUDIO,
  SETTINGS_ENABLE_FRAME_PACER,
  SETTINGS_CENTER_REGION_ONLY,
  SETTINGS_ENABLE_MAPPING,
  SETTINGS_BACK_DEADZONE,
  SETTINGS_SPECIAL_KEYS,
  SETTINGS_ENABLE_SPECIAL_KEYS,
  SETTINGS_ENABLE_PSBUTTON_CAPTURE,
  // SETTINGS_HOTKEYS, // Eliminado: hotkeys fijos
  SETTINGS_CONTROLLER_TYPE,
  SETTINGS_SWAP_SHOULDER_BUTTONS, // NUEVO: Swap R1/L1 <-> R2/L2
  SETTINGS_MOUSE_ACCEL,
  SETTINGS_KEYBOARD_LAYOUT,
  SETTINGS_TOUCH_MODE_SELECT, // Nuevo: selección de modo táctil exclusivo
  SETTINGS_BOOST_CLOCKS,
  SETTINGS_MATCH_REFRESH_RATE,
  SETTINGS_LOW_LATENCY_AUDIO,
  SETTINGS_DISPLAY_MODE,
  SETTINGS_PACKET_SIZE,
  SETTINGS_LANGUAGE
};

/* settings_loop returns this to rebuild the menu (labels change with the language) */
#define SETTINGS_RELOAD 2

enum {
  SETTINGS_VIEW_RESOLUTION,
  SETTINGS_VIEW_FPS,
  SETTINGS_VIEW_BITRATE,
  SETTINGS_VIEW_SOPS,
  SETTINGS_VIEW_ENABLE_FRAME_INVAL,
  SETTINGS_VIEW_ENABLE_STREAM_OPTIMIZE,
  SETTINGS_VIEW_ENABLE_VITA_VBLANK_WAIT,
  SETTINGS_VIEW_ENABLE_MOTION_CONTROLS, //Metalface
  SETTINGS_VIEW_MOTION_CONTROLS_SCALAR_X,
  SETTINGS_VIEW_MOTION_CONTROLS_SCALAR_Y,
  SETTINGS_VIEW_ENABLE_DOUBLE_TAP_SPRINT, //Metalface
  SETTINGS_VIEW_DOUBLE_TAP_SPRINT_STEP_TIME,
  SETTINGS_VIEW_SAVE_DEBUG_LOG,
  SETTINGS_VIEW_DISABLE_POWERSAVE,
  SETTINGS_VIEW_JP_LAYOUT,
  SETTINGS_VIEW_SHOW_FPS,
  SETTINGS_VIEW_LOCAL_AUDIO,
  SETTINGS_VIEW_ENABLE_FRAME_PACER,
  SETTINGS_VIEW_CENTER_REGION_ONLY,
  SETTINGS_VIEW_ENABLE_MAPPING,
  SETTINGS_VIEW_BACK_DEADZONE,
  SETTINGS_VIEW_SPECIAL_KEYS,
  SETTINGS_VIEW_ENABLE_SPECIAL_KEYS,
  SETTINGS_VIEW_ENABLE_PSBUTTON_CAPTURE,
  // SETTINGS_VIEW_HOTKEYS, // Eliminado: hotkeys fijos
  SETTINGS_VIEW_CONTROLLER_TYPE,
  SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS, // NUEVO: Swap R1/L1 <-> R2/L2
  SETTINGS_VIEW_MOUSE_ACCEL,
  SETTINGS_VIEW_KEYBOARD_LAYOUT,
  SETTINGS_VIEW_TOUCH_MODE_SELECT, // Vista para modo táctil exclusivo
  SETTINGS_VIEW_BOOST_CLOCKS,
  SETTINGS_VIEW_MATCH_REFRESH_RATE,
  SETTINGS_VIEW_LOW_LATENCY_AUDIO,
  SETTINGS_VIEW_DISPLAY_MODE,
  SETTINGS_VIEW_PACKET_SIZE,
  SETTINGS_VIEW_LANGUAGE,

  SETTINGS_VIEW_MAX_COUNT,
};

static int SETTINGS_VIEW_IDX[SETTINGS_VIEW_MAX_COUNT];
// Variable global para swap de botones
bool swap_shoulder_buttons = false;
// Variable global para modo táctil exclusivo
int touch_mode_select = 0;

// _countof only works for variable allocated on the stack, not from malloc (sizeof(i) will be incorrect).
#define _countof(i) (sizeof(i) / sizeof((i)[0]))
#define _move_idx_in_array(a, f, i) move_idx_in_array((a), _countof(a), (f), (i))
static int move_idx_in_array(char *array[], int count, char *find, int index_dist) {
  int i = 0;
  for (; i < count; i++) {
    if (strcmp(find, array[i]) == 0) {
      i += index_dist;
      break;
    }
  }

  if (i >= count) {
    return count - 1;
  } else if (i < 0) {
    return 0;
  } else {
    return i;
  }
}


static int settings_hover_id = -1;

/* One or two lines explaining the selected option, under the settings panel. */
static const char *settings_description(int id) {
  switch (id) {
    case SETTINGS_BITRATE: return tr("Video bitrate. At 960x544, try 6 to 10 Mb/s: a lower bitrate means fewer losses on Wi-Fi.");
    case SETTINGS_DISPLAY_MODE: return tr("direct: original behaviour. low latency: shows the newest frame at once. smooth: one frame per screen refresh, steadier (+1 frame of latency).");
    case SETTINGS_LOW_LATENCY_AUDIO: return tr("Sound in 5 ms chunks instead of 20 ms, built-up delay is dropped automatically. Turn off if the sound crackles.");
    case SETTINGS_BOOST_CLOCKS: return tr("CPU at 444 MHz and GPU at 222 MHz while streaming. Uses a little more battery.");
    case SETTINGS_MATCH_REFRESH_RATE: return tr("The PC encodes at the exact refresh rate of the Vita screen, avoiding regular duplicates and gaps.");
    case SETTINGS_PACKET_SIZE: return tr("1392: fewer packets per frame, recommended on a home network. 1024: original value.");
    case SETTINGS_ENABLE_FRAME_PACER: return tr("Original frame pacer. Only used in direct display mode.");
    case SETTINGS_ENABLE_VITA_VBLANK_WAIT: return tr("Direct mode only: waits for the screen refresh. Less tearing, a bit more latency.");
    case SETTINGS_SHOW_FPS: return tr("Shows the stream statistics at the top of the screen (frames, latency, network, sound).");
    case SETTINGS_SAVE_DEBUG_LOG: return tr("Detailed log in the Moonlight folder. Slows things down a little: only for troubleshooting.");
    case SETTINGS_LANGUAGE: return tr("Interface language. Takes effect immediately.");
    default: return NULL;
  }
}

static void settings_draw(void) {
  const char *text = settings_description(settings_hover_id);
  if (!text)
    return;
  vita2d_font *f = theme_font();
  /* simple two-line wrap at the last space that fits */
  char line1[256], line2[256];
  snprintf(line1, sizeof(line1), "%s", text);
  line2[0] = 0;
  int max_w = 640;
  if (theme_text_width(f, 14, line1) > max_w) {
    int cut = (int)strlen(line1);
    while (cut > 0) {
      while (cut > 0 && line1[cut] != ' ') cut--;
      line1[cut] = 0;
      if (theme_text_width(f, 14, line1) <= max_w) break;
      line1[cut] = ' ';
      cut--;
    }
    snprintf(line2, sizeof(line2), "%s", text + cut + 1);
  }
  vita2d_font_draw_text(f, 160, 488, TH_TEXT3, 14, line1);
  if (line2[0])
    vita2d_font_draw_text(f, 160, 506, TH_TEXT3, 14, line2);
}

static int settings_loop(int id, void *context, const input_data *input) {
  menu_entry *menu = context;
  settings_hover_id = id;
  bool did_change = 0;
  bool left = (input->buttons & SCE_CTRL_LEFT) && (input->buttons & SCE_CTRL_HOLD) == 0;
  bool right = (input->buttons & SCE_CTRL_RIGHT) && (input->buttons & SCE_CTRL_HOLD) == 0;

  char current[256];
  int new_idx;

  if (id == SETTINGS_TOUCH_MODE_SELECT) {
    if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
      static const char* mode_names[] = {"Off", "DS4 Touchpad", "Mouse Absolute", "Tablet (Sunshine)"};
      strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_TOUCH_MODE_SELECT]].subname, tr(mode_names[config.touchscreen_mode]));
      return 0;
    }
    config.touchscreen_mode = (config.touchscreen_mode + 1) % 4;
    static const char* mode_names[] = {"Off", "DS4 Touchpad", "Mouse Absolute", "Tablet (Sunshine)"};
    strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_TOUCH_MODE_SELECT]].subname, tr(mode_names[config.touchscreen_mode]));
    // Solo activar touchabsolute_enable en modo Mouse Absolute
    touchabsolute_enable(config.touchscreen_mode == 2);
    did_change = 1;
    return 0;
  }
  switch (id) {
    case SETTINGS_SWAP_SHOULDER_BUTTONS: {
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      swap_shoulder_buttons = !swap_shoulder_buttons;
      config.swap_shoulder_buttons = swap_shoulder_buttons;
      did_change = 1;
      strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS]].subname, swap_shoulder_buttons ? tr("yes") : tr("no"));
      // Exclusividad: desactivar mapping si swap está activo
      if (swap_shoulder_buttons) {
        if (config.mapping) {
          free(config.mapping);
          config.mapping = NULL;
          strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_ENABLE_MAPPING]].subname, tr("no"));
        }
      }
      break;
    }
    case SETTINGS_ENABLE_MAPPING: {
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      if (config.mapping) {
        free(config.mapping);
        config.mapping = NULL;
        strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_ENABLE_MAPPING]].subname, tr("no"));
      } else {
        config.mapping = strdup("mappings/vita.conf");
        strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_ENABLE_MAPPING]].subname, tr("yes"));
        // Si se activa mapping, desactivar swap
        swap_shoulder_buttons = 0;
        config.swap_shoulder_buttons = 0;
        strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS]].subname, tr("no"));
      }
      break;
    }
    // Eliminados SETTINGS_ABSOLUTE_MOUSE y SETTINGS_TOUCHSCREEN_MODE: ahora todo es por touchscreen_mode
  }


  if (!vitavideo_initialized() && !support_resolution_count) {
    for (int i = 0; i < MAX_RESOLUTION; i++) {
      SceVideodecQueryInitInfoHwAvcdec dec;
      dec.size = sizeof(SceVideodecQueryInitInfoHwAvcdec);
      dec.horizontal = VITA_DECODER_RESOLUTION(RESOLUTIONS[i][0]);
      dec.vertical = VITA_DECODER_RESOLUTION(RESOLUTIONS[i][1]);
      dec.numOfRefFrames = 5;
      dec.numOfStreams = 1;
      int ret = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, &dec);
      sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
      if (ret < 0) {
        // unsupported resolution
        continue;
      }
      support_resolutions[support_resolution_count] = calloc(10, sizeof(char));
      snprintf(support_resolutions[support_resolution_count], 10, "%dx%d", RESOLUTIONS[i][0], RESOLUTIONS[i][1]);
      vita_debug_log("res: %d\n", support_resolution_count);
      vita_debug_log("%s\n", support_resolutions[support_resolution_count]);
      support_resolution_idx[support_resolution_count++] = i;
    }
  }

  switch (id) {
    case SETTINGS_CONTROLLER_TYPE: {
      int idx = get_controller_type_index(config.controller_type);
      if (!left && !right) {
        // Solo refresca el subname
        char options[64];
        int current_idx = get_controller_type_index(config.controller_type);
        snprintf(options, sizeof(options), "%s", controller_type_names[current_idx]);
        strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_CONTROLLER_TYPE]].subname, options);
        break;
      }
      if (left) {
        idx = (idx - 1 + CONTROLLER_TYPE_COUNT) % CONTROLLER_TYPE_COUNT;
        config.controller_type = controller_type_values[idx];
        did_change = 1;
      } else if (right) {
        idx = (idx + 1) % CONTROLLER_TYPE_COUNT;
        config.controller_type = controller_type_values[idx];
        did_change = 1;
      }
      // Actualiza el subname
      char options[64];
      int current_idx = get_controller_type_index(config.controller_type);
      snprintf(options, sizeof(options), "%s", controller_type_names[current_idx]);
      strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_CONTROLLER_TYPE]].subname, options);
      // Guarda la configuración inmediatamente
      ui_settings_save_config();
      break;
    }
    case SETTINGS_RESOLUTION:
      if (!left && !right) {
        break;
      }
      if (vitavideo_initialized()) {
        break;
      }
      //char *resolutions[] = {"960x540", "960x544", "1280x540", "1280x720", "1920x1080"};
      sprintf(current, "%dx%d", config.stream.width, config.stream.height);

      new_idx = move_idx_in_array(support_resolutions, support_resolution_count, current, left ? -1 : +1);
      config.stream.width = RESOLUTIONS[support_resolution_idx[new_idx]][0];
      config.stream.height = RESOLUTIONS[support_resolution_idx[new_idx]][1];

      did_change = 1;
      break;
    case SETTINGS_FPS:
      if (!left && !right) {
          break;
      }
      char *settings[] = {"24", "30", "40", "50", "60"};
      sprintf(current, "%d", config.stream.fps);
      new_idx = _move_idx_in_array(settings, current, left ? -1 : +1);

      switch (new_idx) {
        case 0: config.stream.fps = 24; break; // Movies
        case 1: config.stream.fps = 30; break;
        case 2: config.stream.fps = 40; break;
        case 3: config.stream.fps = 50; break; // PAL
        case 4: config.stream.fps = 60; break; // NTSC
      }

      did_change = 1;
      break;
    case SETTINGS_BITRATE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      {
        char value[512];
        int ret;
        if ((ret = ime_dialog_number(value, tr("Enter bitrate: "), "")) == 0) {
          int bitrate = atoi(value);
          if (bitrate) {
            config.stream.bitrate = bitrate;
            did_change = 1;
          } else {
            display_error(tr("Incorrect bitrate entered: %s"), value);
          }
        }
      }
      break;
    case SETTINGS_SOPS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.sops = !config.sops;
      break;
    case SETTINGS_ENABLE_FRAME_INVAL:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.enable_ref_frame_invalidation = !config.enable_ref_frame_invalidation;
      break;
    case SETTINGS_ENABLE_STREAM_OPTIMIZE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.stream.streamingRemotely = config.stream.streamingRemotely ? 0 : 1;
      break;
    case SETTINGS_ENABLE_VITA_VBLANK_WAIT:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.enable_vita_vblank_wait = config.enable_vita_vblank_wait ? 0 : 1;
      break;
    //Metalface--
    case SETTINGS_ENABLE_MOTION_CONTROLS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      did_change = 1;
      config.enable_motion_controls = config.enable_motion_controls ? 0 : 1;
      break;
    case SETTINGS_MOTION_CONTROLS_SCALAR_X:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      {
        char value[512];
        int ret;
        if ((ret = ime_dialog_number(value, tr("Enter Motion Sensitvity Scalar X"), "")) == 0) {
          float scalar = atof(value);
          if (scalar) {
            config.motion_controls_scalar_x = scalar;
            did_change = 1;
          } else {
            display_error(tr("Incorrect scalar entered: %s"), value);
          }
        }
        break;
      }
    case SETTINGS_MOTION_CONTROLS_SCALAR_Y:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      {
        char value[512];
        int ret;
        if ((ret = ime_dialog_number(value, tr("Enter Motion Sensitvity Scalar Y"), "")) == 0) {
          float scalar = atof(value);
          if (scalar) {
            config.motion_controls_scalar_y = scalar;
            did_change = 1;
          } else {
            display_error(tr("Incorrect scalar entered: %s"), value);
          }
        }
      }
      break;
    case SETTINGS_ENABLE_DOUBLE_TAP_SPRINT:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      did_change = 1;
      config.enable_double_tap_sprint = config.enable_double_tap_sprint ? 0 : 1;
      break;
    case SETTINGS_DOUBLE_TAP_SPRINT_STEP_TIME:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      {
        char value[512];
        int ret;
        if ((ret = ime_dialog_number(value, tr("Enter Sprint Step Time in Milliseconds"), "")) == 0) {
          int stp = atoi(value);
          if (stp) {
            config.double_tap_sprint_step_time = stp;
            did_change = 1;
          } else {
            display_error(tr("Incorrect step time entered: %s"), value);
          }
        }
      }
      break;
    //Metalface--
    case SETTINGS_SAVE_DEBUG_LOG:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.save_debug_log = !config.save_debug_log;
      break;
    case SETTINGS_DISABLE_POWERSAVE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.disable_powersave = !config.disable_powersave;
      break;
    case SETTINGS_JP_LAYOUT:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.jp_layout = !config.jp_layout;
      break;
    case SETTINGS_SHOW_FPS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.show_fps = !config.show_fps;
      break;
    case SETTINGS_LOCAL_AUDIO:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.localaudio = !config.localaudio;
      break;
    case SETTINGS_ENABLE_FRAME_PACER:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.enable_frame_pacer = !config.enable_frame_pacer;
      break;
    case SETTINGS_BOOST_CLOCKS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.boost_clocks = !config.boost_clocks;
      break;
    case SETTINGS_MATCH_REFRESH_RATE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.match_refresh_rate = !config.match_refresh_rate;
      break;
    case SETTINGS_LOW_LATENCY_AUDIO:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.low_latency_audio = !config.low_latency_audio;
      break;
    case SETTINGS_DISPLAY_MODE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.display_mode = (config.display_mode + 1) % 3; // direct -> low latency -> smooth
      break;
    case SETTINGS_PACKET_SIZE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      // 1024 (original) or 1392 (largest multiple of 16 that fits a 1500-byte
      // Ethernet/Wi-Fi frame with headers): fewer, larger packets per frame.
      config.stream.packetSize = config.stream.packetSize == 1392 ? 1024 : 1392;
      break;
    case SETTINGS_CENTER_REGION_ONLY:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.center_region_only = !config.center_region_only;
      break;
    // ...eliminado duplicado, la lógica completa está arriba...
    case SETTINGS_BACK_DEADZONE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      deadzone_settings_menu();
      did_change = 1;
      break;
    case SETTINGS_SPECIAL_KEYS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      special_keys_menu();
      break;
    case SETTINGS_ENABLE_SPECIAL_KEYS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }

      config.enable_front_touchzones = !config.enable_front_touchzones;
      did_change = 1;
      break;
    case SETTINGS_ENABLE_PSBUTTON_CAPTURE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }

      config.enable_psbutton_capture = !config.enable_psbutton_capture;
      did_change = 1;
      break;
    case SETTINGS_MOUSE_ACCEL:
      left = input->buttons & SCE_CTRL_LEFT;
      right = input->buttons & SCE_CTRL_RIGHT;
      if (!left && !right) {
          break;
      }
      if (left) {
        config.mouse_acceleration -= 15;
        if (config.mouse_acceleration < 0) {
          config.mouse_acceleration = 0;
        }
      } else {
        config.mouse_acceleration += 15;
        if (config.mouse_acceleration > 300) {
          config.mouse_acceleration = 300;
        }
      }

      did_change = 1;
      break;
    case SETTINGS_KEYBOARD_LAYOUT:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      keyboard_layout_menu();
      did_change = 1;
      break;
    case SETTINGS_LANGUAGE: {
      bool confirm = (input->buttons & config.btn_confirm) && (input->buttons & SCE_CTRL_HOLD) == 0;
      if (!confirm && !left && !right) {
        break;
      }
      int step = left ? LANG_COUNT - 1 : 1;
      config.language = (i18n_language() + step) % LANG_COUNT;
      i18n_set_language(config.language);
      ui_settings_save_config();
      // every label is already copied into the menu: rebuild it in the new language
      return SETTINGS_RELOAD;
    }
    // Eliminados duplicados de SETTINGS_ABSOLUTE_MOUSE y SETTINGS_TOUCHSCREEN_MODE

  }

  if (!did_change && !settings_loop_setup) {
    return 0;
  }
  settings_loop_setup = 0;

#define MENU_REPLACE(ID, MESSAGE) \
    strcpy(menu[SETTINGS_VIEW_IDX[(ID)]].subname, (MESSAGE))

  sprintf(current, "%dx%d", config.stream.width, config.stream.height);
  MENU_REPLACE(SETTINGS_VIEW_RESOLUTION, current);

  sprintf(current, "%d", config.stream.fps);
  MENU_REPLACE(SETTINGS_VIEW_FPS, current);

  sprintf(current, "%d", config.stream.bitrate);
  MENU_REPLACE(SETTINGS_VIEW_BITRATE, current);

  sprintf(current, "%s", config.sops ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_SOPS, current);

  sprintf(current, "%s", config.enable_ref_frame_invalidation ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_FRAME_INVAL, current);

  sprintf(current, "%s", config.stream.streamingRemotely ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_STREAM_OPTIMIZE, current);

  sprintf(current, "%s", config.enable_vita_vblank_wait ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_VITA_VBLANK_WAIT, current);

  //Metalface
  sprintf(current, "%s", config.enable_motion_controls ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_MOTION_CONTROLS, current);

  sprintf(current, "%f", config.motion_controls_scalar_x);
  MENU_REPLACE(SETTINGS_VIEW_MOTION_CONTROLS_SCALAR_X, current);

  sprintf(current, "%f", config.motion_controls_scalar_y);
  MENU_REPLACE(SETTINGS_VIEW_MOTION_CONTROLS_SCALAR_Y, current);

  sprintf(current, "%s", config.enable_double_tap_sprint ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_DOUBLE_TAP_SPRINT, current);

  sprintf(current, "%u", config.double_tap_sprint_step_time);
  MENU_REPLACE(SETTINGS_VIEW_DOUBLE_TAP_SPRINT_STEP_TIME, current);
  //Metalface

  sprintf(current, "%s", config.disable_powersave ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_DISABLE_POWERSAVE, current);

  sprintf(current, "%s", config.jp_layout ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_JP_LAYOUT, current);

  sprintf(current, "%s", config.show_fps ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_SHOW_FPS, current);

  sprintf(current, "%s", config.localaudio ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_LOCAL_AUDIO, current);

  sprintf(current, "%s", config.enable_frame_pacer ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_FRAME_PACER, current);

  sprintf(current, "%s", config.boost_clocks ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_BOOST_CLOCKS, current);

  sprintf(current, "%s", config.match_refresh_rate ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_MATCH_REFRESH_RATE, current);

  sprintf(current, "%s", config.low_latency_audio ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_LOW_LATENCY_AUDIO, current);

  sprintf(current, "%s", config.display_mode == DISPLAY_MODE_SMOOTH ? tr("smooth")
                         : config.display_mode == DISPLAY_MODE_LOW_LATENCY ? tr("low latency") : tr("direct (original)"));
  MENU_REPLACE(SETTINGS_VIEW_DISPLAY_MODE, current);

  sprintf(current, tr("%d bytes"), config.stream.packetSize);
  MENU_REPLACE(SETTINGS_VIEW_PACKET_SIZE, current);

  MENU_REPLACE(SETTINGS_VIEW_LANGUAGE, i18n_language_name(i18n_language()));

  sprintf(current, "%s", config.center_region_only ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_CENTER_REGION_ONLY, current);

  sprintf(current, "%s", config.save_debug_log ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_SAVE_DEBUG_LOG, current);

  sprintf(current, "%s", config.enable_psbutton_capture ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_PSBUTTON_CAPTURE, current);

  sprintf(current, "%s", config.enable_front_touchzones ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_SPECIAL_KEYS, current);

  sprintf(current, "%s", config.mapping != 0 ? tr("yes") : tr("no"));
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_MAPPING, current);

  sprintf(current, "%dpx,%dpx,%dpx,%dpx",
          config.back_deadzone.top,
          config.back_deadzone.right,
          config.back_deadzone.bottom,
          config.back_deadzone.left);
  MENU_REPLACE(SETTINGS_VIEW_BACK_DEADZONE, current);

  sprintf(current, "%d", config.mouse_acceleration);
  MENU_REPLACE(SETTINGS_VIEW_MOUSE_ACCEL, current);

  // Eliminar subnames antiguos de modos táctiles
  static const char* mode_names[] = {"Off", "DS4 Touchpad", "Mouse Absolute", "Tablet (Sunshine)"};
  strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_TOUCH_MODE_SELECT]].subname, tr(mode_names[config.touchscreen_mode]));
  // No actualizar subnames de absolute_mouse ni touchscreen_mode boolean
  // ...resto de actualizaciones de subname...
  return 0;
}

static int settings_back(void *context) {
  ui_settings_save_config();
  update_layout();
  return 0;
}

// Hotkeys are fixed now; the old hotkeys menu was removed.

static int settings_menu_once(void) {
  menu_entry menu[44]; // was 32 and exactly full; room for the new entries and categories
  int idx = 0;
#define MENU_CATEGORY(NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .disabled = true, .separator = true }; \
    idx++; \
  } while (0)
#define MENU_ENTRY(ID, TAG, NAME, SUFFIX) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .id = (ID), .suffix = (SUFFIX) }; \
    SETTINGS_VIEW_IDX[(TAG)] = idx; \
    idx++; \
  } while(0)
#define MENU_MESSAGE(MESSAGE) \
  do { \
    menu[idx] = (menu_entry) { .name = "", .disabled = true }; \
    snprintf(menu[idx].subname, sizeof(menu[idx].subname), "%s", (MESSAGE)); \
    idx++; \
  } while(0)

  // Language first: after a change the menu is rebuilt with the cursor on the
  // first row, so it lands back on this entry.
  MENU_CATEGORY(tr("General"));
  MENU_ENTRY(SETTINGS_LANGUAGE, SETTINGS_VIEW_LANGUAGE, tr("Language"), ICON_LEFT_RIGHT_ARROWS);

  MENU_CATEGORY(tr("Stream"));
  MENU_ENTRY(SETTINGS_RESOLUTION, SETTINGS_VIEW_RESOLUTION, tr("Resolution"), ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY(SETTINGS_FPS, SETTINGS_VIEW_FPS, "FPS", ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY(SETTINGS_BITRATE, SETTINGS_VIEW_BITRATE, tr("Bitrate"), "");
  MENU_ENTRY(SETTINGS_SOPS, SETTINGS_VIEW_SOPS, tr("Change graphical game settings for performance"), "");
  MENU_ENTRY(SETTINGS_ENABLE_FRAME_INVAL, SETTINGS_VIEW_ENABLE_FRAME_INVAL, tr("Enable reference frame invalidation"), "");
  MENU_ENTRY(SETTINGS_ENABLE_STREAM_OPTIMIZE, SETTINGS_VIEW_ENABLE_STREAM_OPTIMIZE, tr("Enable stream optimization"), "");
  MENU_ENTRY(SETTINGS_LOCAL_AUDIO, SETTINGS_VIEW_LOCAL_AUDIO, tr("Enable local audio"), "");

  MENU_CATEGORY(tr("Performance"));
  MENU_ENTRY(SETTINGS_DISPLAY_MODE, SETTINGS_VIEW_DISPLAY_MODE, tr("Display mode"), "");
  MENU_ENTRY(SETTINGS_LOW_LATENCY_AUDIO, SETTINGS_VIEW_LOW_LATENCY_AUDIO, tr("Low-latency audio"), "");
  MENU_ENTRY(SETTINGS_BOOST_CLOCKS, SETTINGS_VIEW_BOOST_CLOCKS, tr("Max CPU/GPU clocks while streaming"), "");
  MENU_ENTRY(SETTINGS_MATCH_REFRESH_RATE, SETTINGS_VIEW_MATCH_REFRESH_RATE, tr("Send Vita refresh rate to host"), "");
  MENU_ENTRY(SETTINGS_PACKET_SIZE, SETTINGS_VIEW_PACKET_SIZE, tr("Video packet size"), "");
  MENU_ENTRY(SETTINGS_ENABLE_FRAME_PACER, SETTINGS_VIEW_ENABLE_FRAME_PACER, tr("Enable frame pacer"), "");
  MENU_ENTRY(SETTINGS_ENABLE_VITA_VBLANK_WAIT, SETTINGS_VIEW_ENABLE_VITA_VBLANK_WAIT, tr("Enable VITA vblank"), "");

  MENU_CATEGORY(tr("System"));
  MENU_ENTRY(SETTINGS_SAVE_DEBUG_LOG, SETTINGS_VIEW_SAVE_DEBUG_LOG, tr("Enable debug log"), "");
  MENU_ENTRY(SETTINGS_DISABLE_POWERSAVE, SETTINGS_VIEW_DISABLE_POWERSAVE, tr("Disable power save"), "");
  MENU_ENTRY(SETTINGS_JP_LAYOUT, SETTINGS_VIEW_JP_LAYOUT, tr("Swap X & O for Moonlight"), "");
  MENU_ENTRY(SETTINGS_SHOW_FPS, SETTINGS_VIEW_SHOW_FPS, tr("Display streaming FPS"), "");

  MENU_CATEGORY(tr("Input"));

  MENU_ENTRY(SETTINGS_ENABLE_MOTION_CONTROLS, SETTINGS_VIEW_ENABLE_MOTION_CONTROLS, tr("Enable Gyroscope reporting"), "");
  MENU_ENTRY(SETTINGS_ENABLE_DOUBLE_TAP_SPRINT, SETTINGS_VIEW_ENABLE_DOUBLE_TAP_SPRINT, tr("Enable double tap to sprint"), "");
  MENU_ENTRY(SETTINGS_DOUBLE_TAP_SPRINT_STEP_TIME, SETTINGS_VIEW_DOUBLE_TAP_SPRINT_STEP_TIME, tr("Sprint double tap time"), "");
  MENU_ENTRY(SETTINGS_CONTROLLER_TYPE, SETTINGS_VIEW_CONTROLLER_TYPE, tr("Controller type"), ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY(SETTINGS_SWAP_SHOULDER_BUTTONS, SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS, tr("Swap R1/L1 <-> R2/L2"), "");
  MENU_ENTRY(SETTINGS_MOUSE_ACCEL, SETTINGS_VIEW_MOUSE_ACCEL, tr("Mouse acceleration"), ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY(SETTINGS_ENABLE_MAPPING, SETTINGS_VIEW_ENABLE_MAPPING, tr("Enable mapping file"), "");
  char mapping_location_msg[256];
  snprintf(mapping_location_msg, sizeof(mapping_location_msg), tr("Located at %svita.conf"), config.key_dir);
  menu[idx] = (menu_entry) { .name = "", .disabled = true };
  strncpy(menu[idx].subname, mapping_location_msg, sizeof(menu[idx].subname) - 1);
  menu[idx].subname[sizeof(menu[idx].subname) - 1] = '\0';
  idx++;
  MENU_MESSAGE(tr("Example in github repo."));
  MENU_ENTRY(SETTINGS_ENABLE_PSBUTTON_CAPTURE, SETTINGS_VIEW_ENABLE_PSBUTTON_CAPTURE, tr("Enable PS button capture"), "");
  MENU_ENTRY(SETTINGS_BACK_DEADZONE, SETTINGS_VIEW_BACK_DEADZONE, tr("Back touchscreen deadzone"), "");
  MENU_ENTRY(SETTINGS_ENABLE_SPECIAL_KEYS, SETTINGS_VIEW_ENABLE_SPECIAL_KEYS, tr("Enable touchscreen special keys"), "");
  MENU_ENTRY(SETTINGS_SPECIAL_KEYS, SETTINGS_VIEW_SPECIAL_KEYS, tr("Touchscreen special keys"), "");
  // MENU_ENTRY(SETTINGS_HOTKEYS, SETTINGS_VIEW_HOTKEYS, "Configure hotkeys", ""); // Eliminado: hotkeys fijos
  // NUEVO: Opción para usar la pantalla táctil como touchpad DS4
  MENU_ENTRY(SETTINGS_TOUCH_MODE_SELECT, SETTINGS_VIEW_TOUCH_MODE_SELECT, tr("Touchscreen mode"), "");
  MENU_CATEGORY(tr("Keyboard"));
  MENU_ENTRY(SETTINGS_KEYBOARD_LAYOUT, SETTINGS_VIEW_KEYBOARD_LAYOUT, tr("Keyboard layout"), "");

  // Inicializar el subname de todas las opciones antes de mostrar el menú
  char current[256];
  // Resolution
  sprintf(current, "%dx%d", config.stream.width, config.stream.height);
  strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_RESOLUTION]].subname, current);
  // FPS
  sprintf(current, "%d", config.stream.fps);
  strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_FPS]].subname, current);
  // Bitrate
  sprintf(current, "%d", config.stream.bitrate);
  strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_BITRATE]].subname, current);
  // Controller type
  int current_idx = get_controller_type_index(config.controller_type);
  snprintf(current, sizeof(current), "%s", controller_type_names[current_idx]);
  strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_CONTROLLER_TYPE]].subname, current);
  // Swap shoulder buttons
  swap_shoulder_buttons = config.swap_shoulder_buttons;
  strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS]].subname, swap_shoulder_buttons ? tr("yes") : tr("no"));
  // Touchscreen mode
  static const char* mode_names[] = {"Off", "DS4 Touchpad", "Mouse Absolute", "Tablet (Sunshine)"};
  strcpy(menu[SETTINGS_VIEW_IDX[SETTINGS_VIEW_TOUCH_MODE_SELECT]].subname, tr(mode_names[config.touchscreen_mode]));
  // Puedes agregar aquí más inicializaciones si quieres que otras opciones también muestren su valor actual al abrir el menú

  settings_loop_setup = 1;
  assert(idx <= (int)(sizeof(menu) / sizeof(menu[0])));
  settings_hover_id = -1;
  int ret = display_menu(menu, idx, NULL, &settings_loop, &settings_back, &settings_draw, &menu);
  return ret;
}

int ui_settings_menu() {
  int ret;
  do {
    ret = settings_menu_once();
  } while (ret == SETTINGS_RELOAD);
  return ret;
}

void ui_settings_save_config() {
  vita_debug_log("[DEBUG] Guardando configuración:");
  vita_debug_log("  touchscreen_mode = %d", config.touchscreen_mode);
  vita_debug_log("  swap_shoulder_buttons = %d", config.swap_shoulder_buttons);
  vita_debug_log("  controller_type = %d", config.controller_type);
  vita_debug_log("  enable_mapping = %s", config.mapping ? "yes" : "no");
  vita_debug_log("  show_fps = %d", config.show_fps);
  vita_debug_log("  enable_motion_controls = %d", config.enable_motion_controls);
  vita_debug_log("  double_tap_sprint = %d", config.enable_double_tap_sprint);
  vita_debug_log("  double_tap_sprint_step_time = %u", config.double_tap_sprint_step_time);
  vita_debug_log("  mouse_acceleration = %d", config.mouse_acceleration);
  vita_debug_log("  stream.width = %d", config.stream.width);
  vita_debug_log("  stream.height = %d", config.stream.height);
  vita_debug_log("  stream.fps = %d", config.stream.fps);
  vita_debug_log("  stream.bitrate = %d", config.stream.bitrate);
  vita_debug_log("  save_debug_log = %d", config.save_debug_log);
  vita_debug_log("  disable_powersave = %d", config.disable_powersave);
  vita_debug_log("  jp_layout = %d", config.jp_layout);
  vita_debug_log("  localaudio = %d", config.localaudio);
  vita_debug_log("  enable_frame_pacer = %d", config.enable_frame_pacer);
  vita_debug_log("  center_region_only = %d", config.center_region_only);
  vita_debug_log("  enable_ref_frame_invalidation = %d", config.enable_ref_frame_invalidation);
  vita_debug_log("  stream.streamingRemotely = %d", config.stream.streamingRemotely);
  vita_debug_log("  enable_vita_vblank_wait = %d", config.enable_vita_vblank_wait);
  config_save(config_path, &config);
  vita_debug_log("[DEBUG] Configuración guardada en %s", config_path);
}

