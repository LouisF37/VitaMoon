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
#include "platform.h"

#include "input/vita.h"
#include "input/touchabsolute.h"
#include "input/keyboardkeys.h"
#include "keyboardsystem.h"

#include <Limelight.h>

#include <stdio.h>
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
#include "util.h"
#include "power/vita.h"
#include "input/motion.h"

#include "debug.h"
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

void loop_forever(void) {
  while (connection_is_ready()) {
    sceKernelDelayThread(100 * 1000);
  }
}

static void vita_init() {
  sceShellUtilInitEvents(0);

  // Seed OpenSSL with Sony-grade random number generator
  char random_seed[0x40] = {0};
  sceKernelGetRandomNumber(random_seed, sizeof(random_seed));
  RAND_seed(random_seed, sizeof(random_seed));
  OpenSSL_add_all_algorithms();

  // This is only used for PIN codes, doesn't really matter
  srand(time(NULL));

  #ifdef __vita__
  printf("Vita Moonlight %d.%d.%d (%s)\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, COMPILE_OPTIONS);
  #endif

  int ret;

  // Load Vita network 
  net_param.memory = net_memory;
 
  ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  if (ret < 0) {
    printf("Net module was unable to load!");
    loop_forever();
  }

  ret = sceNetInit(&net_param);
  if (ret < 0) {
    printf("Net init failed!");
    loop_forever();
  }
  
  ret = sceNetCtlInit();
  if (ret < 0) {
    printf("Net Ctl init failed!");
    loop_forever();
  }

  ret = curl_global_init(CURL_GLOBAL_ALL);
  if (ret < 0) {
    printf("CURL init failed!");
    loop_forever();
  }

  ret = vita_debug_init();
  if (ret != true) {
    printf("Debug log mutex init failed!");
    loop_forever();
  }
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
    vita_debug_log("Vita nickname not readable (0x%08X): pairing as PS Vita\n", ret);
    return;
  }
  gs_set_device_name((const char *)nickname);
}

int main(int argc, char* argv[]) {
  psvDebugScreenInit();
  vita_init();

  if (!vitapower_init()) {
    printf("Failed to init power!");
    loop_forever();
  }

  if (!vitainput_init()) {
    printf("Failed to init input!");
    loop_forever();
  }

  if (!vita_motion_init()) {
    printf("Failed to init motion input!");
    loop_forever();
  }

  char out_path[MOONLIGHT_PATH_MAX] = {0};
  char out_key_dir[MOONLIGHT_PATH_MAX] = {0};
  check_and_create_moonlight_dir(out_path, out_key_dir);
  config_path = out_path;
  strcpy(config.key_dir, out_key_dir);
  vita_debug_log("[Moonlight] Data folder: %s", config.key_dir);
  // The config is not saved before every value is initialized
  config_parse(argc, argv, &config);

  // Interface language (English unless the user picked another one in Settings).
  i18n_set_language(config.language);

  // Restore the absolute touch state at startup
  touchabsolute_enable(config.touchscreen_mode == 2);

  // Apply the saved keyboard layout after loading the config
  keyboardsystem_set_layout((KeyboardLayout)config.keyboard_layout);

  vitapower_config(config);
  vitainput_config(config);

  char log_path[MOONLIGHT_PATH_MAX] = {0};
  snprintf(log_path, sizeof(log_path), "%s/moonlight.log", config.key_dir);
  config.log_file = fopen(log_path, "w");
  // After the log opens, so that a failure to read the nickname is recorded.
  set_pairing_device_name();

  load_all_known_devices();

  gui_loop();
}
