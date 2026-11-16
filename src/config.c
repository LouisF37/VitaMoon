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

#include "config.h"
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <ini.h>
#include "input/vita.h"
#include "debug.h"

extern char* strdup(const char*);

#include <psp2/kernel/sysmem.h>

#define MOONLIGHT_PATH "/moonlight"
#define USER_PATHS "."
#define DEFAULT_CONFIG_DIR "/.config"
#define DEFAULT_CACHE_DIR "/.cache"

#define write_config_string(fd, key, value) fprintf(fd, "%s = %s\n", key, value)
#define write_config_int(fd, key, value) fprintf(fd, "%s = %d\n", key, value)
#define write_config_hex(fd, key, value) fprintf(fd, "%s = %X\n", key, value)
#define write_config_bool(fd, key, value) fprintf(fd, "%s = %s\n", key, value?"true":"false");
#define write_config_float(fd, key, value) fprintf(fd, "%s = %f\n", key, value)
#define write_config_section(fd, key) fprintf(fd, "\n[%s]\n", key)

CONFIGURATION config;
char *config_path;

bool inputAdded = false;
static bool mapped = true;
const char* audio_device = NULL;

static int ini_handle(void *out, const char *section, const char *name,
                      const char *value) {
#define HEX(v) strtol((v), NULL, 16)
#define INT(v) atoi((v))
#define BOOL(v) strcmp((v), "true") == 0
#define STR(v) strdup((v))
#define FLT(v) atof((v))

  PCONFIGURATION config = (PCONFIGURATION)out;
  if (strcmp(section, "backtouchscreen_deadzone") == 0) {
    if (strcmp(name, "top") == 0) {
      config->back_deadzone.top = INT(value);
    } else if (strcmp(name, "right") == 0) {
      config->back_deadzone.right = INT(value);
    } else if (strcmp(name, "bottom") == 0) {
      config->back_deadzone.bottom = INT(value);
    } else if (strcmp(name, "left") == 0) {
      config->back_deadzone.left = INT(value);
    }
    else if (strcmp(name, "controller_type") == 0) {
      config->controller_type = INT(value);
    } else if (strcmp(name, "swap_shoulder_buttons") == 0) {
      config->swap_shoulder_buttons = BOOL(value);
    } else if (strcmp(name, "key_dir") == 0) {
      strncpy(config->key_dir, value, sizeof(config->key_dir)-1);
      config->key_dir[sizeof(config->key_dir)-1] = '\0';
    }
  } else if (strcmp(section, "special_keys") == 0) {
    if (strcmp(name, "nw") == 0) {
      config->special_keys.nw = HEX(value);
    } else if (strcmp(name, "ne") == 0) {
      config->special_keys.ne = HEX(value);
    } else if (strcmp(name, "sw") == 0) {
      config->special_keys.sw = HEX(value);
    } else if (strcmp(name, "se") == 0) {
      config->special_keys.se = HEX(value);
    } else if (strcmp(name, "offset") == 0) {
      config->special_keys.offset = INT(value);
    } else if (strcmp(name, "size") == 0) {
      config->special_keys.size = INT(value);
    }
  } else {
    if (strcmp(name, "address") == 0) {
      config->address = STR(value);
    } else if (strcmp(name, "width") == 0) {
      config->stream.width = INT(value);
    } else if (strcmp(name, "height") == 0) {
      config->stream.height = INT(value);
    } else if (strcmp(name, "fps") == 0) {
      config->stream.fps = INT(value);
    } else if (strcmp(name, "bitrate") == 0) {
      // A bitrate saved by an older version was chosen by hand.
      config->stream.bitrate = INT(value);
      config->auto_bitrate = false;
    } else if (strcmp(name, "auto_bitrate") == 0) {
      config->auto_bitrate = BOOL(value);
    } else if (strcmp(name, "packet_size") == 0) {
      // Earlier versions wrote "packetsize" but never read it back, so a value
      // under that key was never a choice that took effect: it is ignored and
      // the measured default applies. An out-of-range value falls back to it.
      int size = INT(value);
      config->stream.packetSize = size >= PACKET_SIZE_MIN && size <= PACKET_SIZE_MAX ? size : PACKET_SIZE_DEFAULT;
    } else if (strcmp(name, "sops") == 0) {
      config->sops = BOOL(value);
    } else if (strcmp(name, "localaudio") == 0) {
      config->localaudio = BOOL(value);
    } else if (strcmp(name, "enable_frame_pacer") == 0) {
      config->enable_frame_pacer = BOOL(value);
    } else if (strcmp(name, "center_region_only") == 0) {
      config->center_region_only = BOOL(value);
    } else if (strcmp(name, "disable_powersave") == 0) {
      config->disable_powersave = BOOL(value);
    } else if (strcmp(name, "jp_layout") == 0) {
      config->jp_layout = BOOL(value);
    } else if (strcmp(name, "show_fps") == 0) {
      config->show_fps = BOOL(value);
    } else if (strcmp(name, "save_debug_log") == 0) {
      config->save_debug_log = BOOL(value);
    } else if (strcmp(name, "mapping") == 0) {
      config->mapping = STR(value);
    } else if (strcmp(name, "mouse_acceleration") == 0) {
      config->mouse_acceleration = INT(value);
    } else if (strcmp(name, "enable_ref_frame_invalidation") == 0) {
      config->enable_ref_frame_invalidation = BOOL(value);
    } else if (strcmp(name, "enable_remote_stream_optimization") == 0) {
      config->stream.streamingRemotely = INT(value);
    } else if (strcmp(name, "enable_vita_vblank_wait") == 0) {
      config->enable_vita_vblank_wait = BOOL(value);
    } else if (strcmp(name, "boost_clocks") == 0) {
      config->boost_clocks = BOOL(value);
    } else if (strcmp(name, "match_refresh_rate") == 0) {
      config->match_refresh_rate = BOOL(value);
    } else if (strcmp(name, "low_latency_audio") == 0) {
      config->low_latency_audio = BOOL(value);
    } else if (strcmp(name, "display_mode") == 0) {
      config->display_mode = INT(value);
    } else if (strcmp(name, "decoder_catchup") == 0) {
      config->decoder_catchup = BOOL(value);
    } else if (strcmp(name, "http_keepalive") == 0) {
      config->http_keepalive = BOOL(value);
    } else if (strcmp(name, "ui_msaa") == 0) {
      config->ui_msaa = BOOL(value);
    } else if (strcmp(name, "decode_yuv") == 0) {
      config->decode_yuv = BOOL(value);
    } else if (strcmp(name, "video_reorder_wait_ms") == 0) {
      int wait = INT(value);
      config->video_reorder_wait_ms = wait < 0 ? 0 : (wait > 50 ? 50 : wait);
    } else if (strcmp(name, "thread_pinning") == 0) {
      config->thread_pinning = BOOL(value);
    } else if (strcmp(name, "input_sync") == 0) {
      config->input_sync = BOOL(value);
    } else if (strcmp(name, "auto_reconnect") == 0) {
      config->auto_reconnect = BOOL(value);
    } else if (strcmp(name, "enable_motion_controls") == 0) {
      config->enable_motion_controls = BOOL(value);
    } else if (strcmp(name, "enable_front_touchzones") == 0) {
      config->enable_front_touchzones = BOOL(value);
    } else if(strcmp(name, "enable_psbutton_capture") == 0) {
      config->enable_psbutton_capture = BOOL(value);
    } else if (strcmp(name, "enable_double_tap_sprint") == 0) {
      config->enable_double_tap_sprint = BOOL(value);
    } else if (strcmp(name, "double_tap_sprint_step_time") == 0) {
      config->double_tap_sprint_step_time = INT(value);
    } else if (strcmp(name, "motion_controls_scalar_x") == 0) {
      config->motion_controls_scalar_x = FLT(value);
    } else if (strcmp(name, "motion_controls_scalar_y") == 0) {
      config->motion_controls_scalar_y = FLT(value);
    } else if (strcmp(name, "keyboard_layout") == 0) {
      config->keyboard_layout = INT(value);
    } else if (strcmp(name, "language") == 0) {
      config->language = INT(value);
    } else if (strcmp(name, "theme") == 0) {
      config->theme = INT(value);
    } else if (strcmp(name, "touchscreen_mode") == 0) {
      config->touchscreen_mode = atoi(value);
    } else if (strcmp(name, "controller_type") == 0) {
      config->controller_type = INT(value);
    } else if (strcmp(name, "swap_shoulder_buttons") == 0) {
      config->swap_shoulder_buttons = BOOL(value);
    }
  }
  return 0;
}

bool config_file_parse(char* filename, PCONFIGURATION config) {
  return ini_parse(filename, ini_handle, config);
}

/* Values from a settings file written by another version or edited by hand:
 * anything outside what the app can use goes back to its default (idea from
 * alex4580/vita-moonlight's config_sanitize). */
void config_sanitize(PCONFIGURATION config) {
  if (config->stream.width < 64 || config->stream.width > 1920 ||
      config->stream.height < 64 || config->stream.height > 1080) {
    config->stream.width = 960;
    config->stream.height = 544;
  }
  if (config->stream.fps != -1 && (config->stream.fps < 24 || config->stream.fps > 60))
    config->stream.fps = 60;
  if (config->stream.bitrate != -1 && (config->stream.bitrate < 500 || config->stream.bitrate > 50000)) {
    config->stream.bitrate = -1;
    config->auto_bitrate = true;
  }
  if (config->stream.packetSize < PACKET_SIZE_MIN || config->stream.packetSize > PACKET_SIZE_MAX)
    config->stream.packetSize = PACKET_SIZE_DEFAULT;
  if (config->display_mode < DISPLAY_MODE_DIRECT || config->display_mode > DISPLAY_MODE_PACED)
    config->display_mode = DISPLAY_MODE_SMOOTH;
  if (config->touchscreen_mode < 0 || config->touchscreen_mode > 3)
    config->touchscreen_mode = 0;
  if (config->controller_type < 1 || config->controller_type > 4)
    config->controller_type = 2;
  if (config->mouse_acceleration < 15 || config->mouse_acceleration > 300)
    config->mouse_acceleration = 150;
  if (!(config->motion_controls_scalar_x >= 0.1f && config->motion_controls_scalar_x <= 5.0f))
    config->motion_controls_scalar_x = 1.2f;
  if (!(config->motion_controls_scalar_y >= 0.1f && config->motion_controls_scalar_y <= 5.0f))
    config->motion_controls_scalar_y = 0.8f;
  if (config->double_tap_sprint_step_time < 50 || config->double_tap_sprint_step_time > 1000)
    config->double_tap_sprint_step_time = 200;
  if (config->language < 0 || config->language > 2)
    config->language = 0;
  if (config->theme < 0 || config->theme >= CONFIG_THEME_COUNT)
    config->theme = 0;
  if (config->keyboard_layout < 0 || config->keyboard_layout >= CONFIG_KEYBOARD_LAYOUT_COUNT)
    config->keyboard_layout = CONFIG_KEYBOARD_TEXT;
}

void config_save(const char* filename, PCONFIGURATION config) {
  FILE* fd = fopen(filename, "w");
  if (fd == NULL) {
    fprintf(stderr, "Can't open configuration file: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  if (config->address)
    write_config_string(fd, "address", config->address);


  if (config->mapping)
    write_config_string(fd, "mapping", config->mapping);

  write_config_int(fd, "width", config->stream.width);
  write_config_int(fd, "height", config->stream.height);
  write_config_int(fd, "fps", config->stream.fps);
  // "bitrate" first, "auto_bitrate" after it: the parser treats a bitrate with
  // no auto_bitrate line (older versions) as a manual choice.
  write_config_int(fd, "bitrate", config->stream.bitrate);
  write_config_bool(fd, "auto_bitrate", config->auto_bitrate);
  write_config_int(fd, "packet_size", config->stream.packetSize);
  if (!config->sops)
    write_config_bool(fd, "sops", config->sops);
  if (config->localaudio)
    write_config_bool(fd, "localaudio", config->localaudio);

  if (config->app && strcmp(config->app, "Steam") != 0)
    write_config_string(fd, "app", config->app);

  write_config_string(fd, "key_dir", config->key_dir); // key_dir at the top level
  write_config_bool(fd, "enable_frame_pacer", config->enable_frame_pacer);
  write_config_bool(fd, "center_region_only", config->center_region_only);
  write_config_bool(fd, "disable_powersave", config->disable_powersave);
  write_config_bool(fd, "jp_layout", config->jp_layout);
  write_config_bool(fd, "show_fps", config->show_fps);
  write_config_bool(fd, "save_debug_log", config->save_debug_log);
  write_config_bool(fd, "enable_front_touchzones", config->enable_front_touchzones);

  write_config_int(fd, "mouse_acceleration", config->mouse_acceleration);
  write_config_bool(fd, "enable_ref_frame_invalidation", config->enable_ref_frame_invalidation);
  write_config_int(fd, "enable_remote_stream_optimization", config->stream.streamingRemotely);
  write_config_bool(fd, "enable_vita_vblank_wait", config->enable_vita_vblank_wait);
  write_config_bool(fd, "boost_clocks", config->boost_clocks);
  write_config_bool(fd, "match_refresh_rate", config->match_refresh_rate);
  write_config_bool(fd, "low_latency_audio", config->low_latency_audio);
  write_config_int(fd, "display_mode", config->display_mode);
  write_config_bool(fd, "decoder_catchup", config->decoder_catchup);
  write_config_bool(fd, "http_keepalive", config->http_keepalive);
  write_config_bool(fd, "ui_msaa", config->ui_msaa);
  write_config_bool(fd, "decode_yuv", config->decode_yuv);
  write_config_int(fd, "video_reorder_wait_ms", config->video_reorder_wait_ms);
  write_config_bool(fd, "thread_pinning", config->thread_pinning);
  write_config_bool(fd, "input_sync", config->input_sync);
  write_config_bool(fd, "auto_reconnect", config->auto_reconnect);
  write_config_bool(fd, "enable_motion_controls", config->enable_motion_controls);
  write_config_bool(fd, "enable_psbutton_capture", config->enable_psbutton_capture);
  write_config_bool(fd, "enable_double_tap_sprint", config->enable_double_tap_sprint);
  write_config_int(fd, "double_tap_sprint_step_time", config->double_tap_sprint_step_time);
  write_config_float(fd, "motion_controls_scalar_x", config->motion_controls_scalar_x);
  write_config_float(fd, "motion_controls_scalar_y", config->motion_controls_scalar_y);
  write_config_int(fd, "keyboard_layout", config->keyboard_layout);
  write_config_int(fd, "language", config->language);
  write_config_int(fd, "theme", config->theme);
  write_config_int(fd, "touchscreen_mode", config->touchscreen_mode);
  write_config_bool(fd, "swap_shoulder_buttons", config->swap_shoulder_buttons); // top level
  write_config_int(fd, "controller_type", config->controller_type); // top level
  

  write_config_section(fd, "backtouchscreen_deadzone");
  write_config_int(fd, "top",     config->back_deadzone.top);
  write_config_int(fd, "right",   config->back_deadzone.right);
  write_config_int(fd, "bottom",  config->back_deadzone.bottom);
  write_config_int(fd, "left",    config->back_deadzone.left);

  write_config_section(fd, "special_keys");
  write_config_hex(fd, "nw",      config->special_keys.nw);
  write_config_hex(fd, "ne",      config->special_keys.ne);
  write_config_hex(fd, "sw",      config->special_keys.sw);
  write_config_hex(fd, "se",      config->special_keys.se);
  write_config_int(fd, "offset",  config->special_keys.offset);
  write_config_int(fd, "size",    config->special_keys.size);

  fclose(fd);
}

/*
 * Automatic bitrate. The Vita's Wi-Fi is 2.4 GHz 802.11n with one antenna: a
 * moderate bitrate keeps key frames small enough to arrive without loss, and
 * hardware H.264 encoders look clean at these values for these sizes.
 */
int config_auto_bitrate(int width, int height, int fps) {
  long pixels = (long)width * height;
  int kbps;
  if (pixels <= 960L * 544)
    kbps = 8000;
  else if (pixels <= 1280L * 720)
    kbps = 10000;
  else
    kbps = 15000;
  if (fps <= 30)
    kbps = kbps * 5 / 8;
  return kbps;
}

void config_apply_auto_bitrate(PCONFIGURATION config) {
  if (config->auto_bitrate)
    config->stream.bitrate = config_auto_bitrate(config->stream.width, config->stream.height, config->stream.fps);
}

void update_layout() {
  if (config.jp_layout) {
    config.btn_confirm = SCE_CTRL_CIRCLE;
    config.btn_cancel = SCE_CTRL_CROSS;
  }
  else {
    config.btn_confirm = SCE_CTRL_CROSS;
    config.btn_cancel = SCE_CTRL_CIRCLE;
  }
}

void config_parse(int argc, char* argv[], PCONFIGURATION config) {
  LiInitializeStreamConfiguration(&config->stream);

  // The Vita screen is 960x544: streaming at that size is the sharpest (one
  // stream pixel per screen pixel) and the lightest to send and decode.
  config->stream.width = 960;
  config->stream.height = 544;
  config->stream.fps = 60;
  config->stream.bitrate = -1;
  config->auto_bitrate = true;
  // 1024, Moonlight's value. Measured on a Vita by juanjsebgarcia/vita-moonlight:
  // 1392-byte packets saved no receive time and lost more (59 vs 43 per minute).
  config->stream.packetSize = PACKET_SIZE_DEFAULT;
  config->stream.streamingRemotely = 0;
  config->stream.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
  config->stream.supportedVideoFormats = VIDEO_FORMAT_H264;
  // The YUV path draws the decoder's output through a *_CSC0 texture, which
  // converts as BT.601 limited range: ask the host for exactly that (these are
  // also moonlight-common-c's defaults). Change both together.
  config->stream.colorSpace = COLORSPACE_REC_601;
  config->stream.colorRange = COLOR_RANGE_LIMITED;

  config->platform = "vita";
  config->model = sceKernelGetModelForCDialog();
  config->app = "Steam";
  config->action = NULL;
  config->address = NULL;
  config->config_file = NULL;
  config->sops = true;
  config->localaudio = false;
  config->fullscreen = true;
  config->unsupported_version = false;
  config->save_debug_log = false;
  config->disable_powersave = true;
  config->jp_layout = false;
  config->show_fps = false;
  config->enable_frame_pacer = true;
  config->center_region_only = false;

  config->enable_front_touchzones = false;
  config->special_keys.nw = INPUT_SPECIAL_KEY_PAUSE | INPUT_TYPE_SPECIAL;
  config->special_keys.sw = SPECIAL_FLAG | INPUT_TYPE_GAMEPAD;
  config->special_keys.offset = 0;
  config->special_keys.size = 150;

  config->mouse_acceleration = 150;
  config->enable_ref_frame_invalidation = false;
  config->enable_vita_vblank_wait = false;
  config->boost_clocks = true;
  config->match_refresh_rate = true;
  config->low_latency_audio = true;
  config->display_mode = DISPLAY_MODE_SMOOTH; // beta default
  config->decoder_catchup = true;
  config->http_keepalive = false;
  config->ui_msaa = true;
  // Measured on a Vita by juanjsebgarcia/vita-moonlight: hardware decode 4.6 -> 2.4 ms.
  config->decode_yuv = true;
  // Same source, ~60 % Wi-Fi signal: 23-37 picture freezes a minute without the
  // wait, 2-8 with 10 ms, 1-3 with 20 ms, same latency estimate.
  config->video_reorder_wait_ms = 20;
  // dubleyu/vita-moonlight-relay spreads the streaming work over the 3 cores.
  config->thread_pinning = true;
  // juanjsebgarcia/vita-moonlight reads the controls in step with the
  // controller's sampling by default (no measurement published).
  config->input_sync = true;
  // A stream lost to a Wi-Fi drop or a sleep starts again on its own, as in
  // VitaRPS5 (reconnection overlay).
  config->auto_reconnect = true;
  config->language = 0; // English
  config->theme = 0;    // Night
  // Text typing works whatever the PC's keyboard layout (an AZERTY PC turns
  // the US key positions into other letters).
  config->keyboard_layout = CONFIG_KEYBOARD_TEXT;
  config->enable_psbutton_capture = true;
  config->enable_double_tap_sprint = false;

  config->double_tap_sprint_step_time = 200;

  config->motion_controls_scalar_x = 1.2;
  config->motion_controls_scalar_y = 0.8;

  config->inputsCount = 0;
  config->mapping = NULL;
  // Do not overwrite key_dir if main.c already set it
  // config->key_dir[0] = 0;
  // Default: PS when unset
  if (config->controller_type < 1 || config->controller_type > 4) {
    config->controller_type = 2;
  }
  // Default: swap off
  // (false when missing from the config)
  // nothing to force here

  char* config_file = config_path;
  if (config_file) {
    config_file_parse(config_file, config);
    config_sanitize(config);
    vita_debug_log("[DEBUG] Config loaded: key_dir = %s, touchscreen_mode = %d, show_fps = %d", config->key_dir, config->touchscreen_mode, config->show_fps);
  }

  update_layout();

  if (config->config_file != NULL)
    config_save(config->config_file, config);

  // Only set the default if still empty
  if (config->key_dir[0] == 0x0) {
    const char *xdg_cache_dir = getenv("XDG_CACHE_DIR");
    if (xdg_cache_dir && xdg_cache_dir[0] != '\0') {
      snprintf(config->key_dir, sizeof(config->key_dir), "%s" MOONLIGHT_PATH, xdg_cache_dir);
    } else {
      const char *home_dir = getenv("HOME");
      if (home_dir && home_dir[0] != '\0') {
        snprintf(config->key_dir, sizeof(config->key_dir), "%s" DEFAULT_CACHE_DIR MOONLIGHT_PATH, home_dir);
      } else {
        snprintf(config->key_dir, sizeof(config->key_dir), "%s" DEFAULT_CACHE_DIR MOONLIGHT_PATH, "");
      }
    }
  }

  if (config->stream.fps == -1)
    config->stream.fps = config->stream.height >= 1080 ? 30 : 60;

  if (config->stream.bitrate <= 0)
    config->auto_bitrate = true;
  config_apply_auto_bitrate(config);

  if (inputAdded) {
    if (!mapped) {
        fprintf(stderr, "Mapping option should be followed by the input to be mapped.\n");
        exit(-1);
    } else if (config->mapping == NULL) {
        fprintf(stderr, "Please specify mapping file as default mapping could not be found.\n");
        exit(-1);
    }
  }
}
