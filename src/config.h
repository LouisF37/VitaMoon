/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015, 2016 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include <Limelight.h>

#include <stdio.h>
#include <stdbool.h>

#include <psp2/ctrl.h>

#define MAX_INPUTS 6

/* Display modes, see src/video/vita.c */
enum { DISPLAY_MODE_DIRECT = 0, DISPLAY_MODE_LOW_LATENCY = 1, DISPLAY_MODE_SMOOTH = 2,
       DISPLAY_MODE_PACED = 3 };

/* Video packet sizes offered in the settings. 1024 is Moonlight's own value. */
#define PACKET_SIZE_DEFAULT 1024
#define PACKET_SIZE_MIN 512
#define PACKET_SIZE_MAX 1392

struct input_config {
  char* path;
  char* mapping;
};

struct touchscreen_deadzone {
  int top, bottom, left, right;
};

struct special_keys {
  int size, offset;
  unsigned int nw, ne, sw, se;
};

/* Number of colour themes (THEME_COUNT in src/gui/theme.h, checked there). */
#define CONFIG_THEME_COUNT 5

typedef struct _CONFIGURATION {
  // static configuration, value will be saved to config file
  STREAM_CONFIGURATION stream;
  char* app;
  char* action;
  char* address;
  char* mapping;
  char* platform;
  uint32_t model;
  char* config_file;
  char key_dir[4096];
  bool sops;
  bool localaudio;
  bool fullscreen;
  bool forcehw;
  bool unsupported_version;
  struct touchscreen_deadzone back_deadzone;
  bool enable_front_touchzones;
  struct special_keys special_keys;
  bool disable_powersave;
  bool jp_layout;
  bool show_fps;
  bool enable_frame_pacer;
  bool center_region_only;
  bool save_debug_log;
  struct input_config inputs[MAX_INPUTS];
  int inputsCount;
  int mouse_acceleration;
  bool enable_ref_frame_invalidation;
  bool enable_vita_vblank_wait;
  bool boost_clocks;        // CPU/GPU/bus at max frequency while streaming
  bool match_refresh_rate;  // tell the host the Vita's exact refresh rate
  bool low_latency_audio;   // 5 ms audio port fed from a ring buffer
  int display_mode;         // DISPLAY_MODE_*
  bool decoder_catchup;     // pull pictures the hardware decoder holds back
  bool auto_bitrate;        // bitrate follows resolution and frame rate
  bool http_keepalive;      // keep the menu connections to the PC open between requests
  bool ui_msaa;             // 4x multisampling (read once, when the graphics start)
  bool decode_yuv;          // the decoder outputs YUV and the GPU converts it (faster)
  int video_reorder_wait_ms; // wait for late video packets before giving a frame up (0 = off)
  bool thread_pinning;      // place the streaming threads on fixed CPU cores
  bool input_sync;          // read the controls once per controller sample instead of every 2 ms
  bool auto_reconnect;      // start a lost stream again (Wi-Fi drop, sleep), src/reconnect.h
  bool enable_motion_controls; //Metalface
  bool enable_psbutton_capture;
  bool enable_double_tap_sprint; //**
  uint32_t double_tap_sprint_step_time; //** -IN MILLISECONDS
  float motion_controls_scalar_x;//**
  float motion_controls_scalar_y;// **/
  FILE *log_file;
  // runtime configuration, value will be recreated at launch
  SceCtrlButtons btn_confirm;
  SceCtrlButtons btn_cancel;
  int pin;
  uint16_t port;
  int keyboard_layout; // 0=EN_US, 1=ES_ES, 2=ES_LATAM
  int language; // interface language: LANG_EN / LANG_FR / LANG_ES (see i18n.h)
  int theme;    // colour theme of the menus: THEME_* in src/gui/theme.h
  int touchscreen_mode; // 0=off, 1=DS4, 2=Mouse absoluto, 3=Tableta multitouch
  int controller_type; // 1: Xbox, 2: PS (default), 3: Nintendo, 4: Generic
  bool swap_shoulder_buttons; // swap R1/L1 <-> R2/L2
} CONFIGURATION, *PCONFIGURATION;

extern CONFIGURATION config;

/* Bitrate (kbps) suited to the Vita's Wi-Fi for a resolution and frame rate. */
int config_auto_bitrate(int width, int height, int fps);
/* Recomputes stream.bitrate when the automatic bitrate is on. */
void config_apply_auto_bitrate(PCONFIGURATION config);
extern char *config_path;

extern bool inputAdded;

bool config_file_parse(char* filename, PCONFIGURATION config);
void config_sanitize(PCONFIGURATION config);
void config_parse(int argc, char* argv[], PCONFIGURATION config);
void config_save(const char* filename, PCONFIGURATION config);
void update_layout();
