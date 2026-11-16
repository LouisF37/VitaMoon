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

#include "connection.h"
#include "configuration.h"
#include "audio.h"
#include "psp2/net/netctl.h"
#include "video.h"
#include "config.h"
#include "app_info.h"
#include "platform.h"

#include "input/vita.h"
#include "input/touchabsolute.h"
#include "input/keyboardkeys.h"
#include "keyboardsystem.h"

#include <Limelight.h>

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include "curl/curl.h"

#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>

#include <psp2/net/net.h>


#include <psp2/io/stat.h>

#include <psp2/shellutil.h>
#include <psp2/sysmodule.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include "client.h"
#include "graphics.h"
#include "device.h"
#include "gui/ui.h"
#include "gui/splash.h"
#include "gui/ui_settings.h"
#include "util.h"
#include "power/vita.h"
#include "input/motion.h"

#include "debug.h"
#include "log.h"
#include "check_dir.h"
#include "i18n.h"

// Network memory pool. Socket receive buffers are carved from it: Moonlight asks
// for a ~2 MB video receive buffer, and VitaRPS5 measured a 4 MiB pool running
// out under sustained LAN streaming (8 MiB fixed it). The original was 1 MiB.
// Static (not malloc) so it does not depend on the heap size.
#define VITA_NET_MEM_SIZE (8 * 1024 * 1024)
static char net_memory[VITA_NET_MEM_SIZE] __attribute__((aligned(64)));

SceNetInitParam net_param = {
  .memory = NULL,
  .size = VITA_NET_MEM_SIZE,
  .flags = 0
};

/* Start-up failed: the message printed just before stays on the screen. The
 * original loop ended at once (no connection is ready at start-up) and the app
 * went on without its network or its controls, to crash further on. */
void loop_forever(void) {
  printf("\nVitaMoon cannot start. Press the PS button to close it.\n");
  while (1)
    sceKernelDelayThread(1000 * 1000);
}

/* The text screen of graphics.c is only for this: it replaces the start-up
 * screen (once the frames sent to it are shown) with the message. */
static void startup_failed(const char *message) {
  splash_abort();
  psvDebugScreenInit();
  printf("%s\n", message);
  loop_forever();
}

static const char *start_network(void) {
  sceShellUtilInitEvents(0);

  // Seed OpenSSL with Sony-grade random number generator
  char random_seed[0x40] = {0};
  sceKernelGetRandomNumber(random_seed, sizeof(random_seed));
  RAND_seed(random_seed, sizeof(random_seed));
  OpenSSL_add_all_algorithms();

  // This is only used for PIN codes, doesn't really matter
  srand(time(NULL));

  // Load Vita network
  net_param.memory = net_memory;

  if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0)
    return "Net module was unable to load!";
  if (sceNetInit(&net_param) < 0)
    return "Net init failed!";
  if (sceNetCtlInit() < 0)
    return "Net Ctl init failed!";
  // Its result was never acted on (the test was "< 0" on a code that is 0 or
  // positive): a failure is recorded, and the start goes on as before.
  CURLcode curl = curl_global_init(CURL_GLOBAL_ALL);
  if (curl != CURLE_OK)
    LOGW("app", "curl_global_init: %d", (int)curl);
  return NULL;
}

/* Pair under the Vita's own nickname (its user name), so the PC's
 * list of paired devices shows it instead of "roth" (juanjsebgarcia/
 * vita-moonlight, commits d6e9859 and f1af08a). Pairings made before keep their
 * old name. On any failure, pairing goes on as "PS Vita". */
static void set_pairing_device_name(void) {
  SceAppUtilInitParam init_param = {0};
  SceAppUtilBootParam boot_param = {0};
  // An error can mean AppUtil is already initialised: the read below decides.
  sceAppUtilInit(&init_param, &boot_param);
  SceChar8 nickname[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE] = {0};
  int ret = sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, nickname, sizeof(nickname));
  if (ret < 0 || nickname[0] == '\0') {
    LOGW("app", "Vita nickname not readable (0x%08X): pairing as PS Vita", ret);
    return;
  }
  gs_set_device_name((const char *)nickname);
}

static const char *start_controls(void) {
  if (!vitapower_init())
    return "Failed to init power!";
  if (!vitainput_init())
    return "Failed to init input!";
  if (!vita_motion_init())
    return "Failed to init motion input!";

  // Restore the absolute touch state at startup
  touchabsolute_enable(config.touchscreen_mode == 2);
  // Apply the saved keyboard layout after loading the config
  keyboardsystem_set_layout((KeyboardLayout)config.keyboard_layout);
  vitapower_config(config);
  vitainput_config(config);
  return NULL;
}

/* The decoder sizes for Settings, found in the background (ui_settings.c). */
static const char *start_decoder_probe(void) {
  ui_settings_probe_start();
  return NULL;
}

static const char *start_hosts(void) {
  set_pairing_device_name();
  load_all_known_devices();
  return NULL;
}

int main(int argc, char* argv[]) {
  /* Before the first frame, only what it needs: the data folder, the log and
   * the settings (colour theme, multisampling, language). The rest loads
   * behind the start-up screen (gui/splash.c), which used to be a text screen
   * with the version on black. */
  char out_path[MOONLIGHT_PATH_MAX] = {0};
  char out_key_dir[MOONLIGHT_PATH_MAX] = {0};
  check_and_create_data_dir(out_path, out_key_dir);
  config_path = out_path;
  strcpy(config.key_dir, out_key_dir);
  // The log opens first, so that everything after it is recorded.
  log_init(config.key_dir);
  // The config is not saved before every value is initialized
  config_parse(argc, argv, &config);
  // The folder found above wins over the one written in the settings file
  // (Lune's folder name, before it was moved).
  strcpy(config.key_dir, out_key_dir);
  log_set_detailed(config.save_debug_log);
  if (check_dir_migrated_from())
    LOGI("app", "data moved from %s (Lune) to %s", check_dir_migrated_from(), config.key_dir);
  {
    SceKernelSystemSwVersion fw;
    memset(&fw, 0, sizeof(fw));
    fw.size = sizeof(fw);
    sceKernelGetSystemSwVersion(&fw);
    LOGI("app", "firmware %s, %s, data folder %s, language %d, detailed log %s", fw.versionString,
         sceKernelGetModel() == SCE_KERNEL_MODEL_VITATV ? "PS TV" : "PS Vita", config.key_dir,
         config.language, config.save_debug_log ? "on" : "off");
  }

  // Interface language (English unless the user picked another one in Settings).
  i18n_set_language(config.language);

  static const splash_step steps[] = {
    {"network", start_network, 3},
    {"controls", start_controls, 1},
    {"decoder probe", start_decoder_probe, 0},
    {"hosts", start_hosts, 1},
  };
  // Loaded at full speed (the clock boost used while connecting).
  vitapower_busy_begin();
  const char *error = splash_run(steps, sizeof(steps) / sizeof(steps[0]));
  vitapower_busy_end();
  if (error)
    startup_failed(error);

  log_system_state("start-up");

  gui_loop();
}
