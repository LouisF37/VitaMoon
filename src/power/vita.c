/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer, Sunguk Lee
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
#include <stdbool.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/registrymgr.h>
#include <pthread.h>
#include "../config.h"
#include "../debug.h"

enum {
  ENABLE_ALL = 0,
  DISABLE_SUSPEND = 1,
};

static int powermode = ENABLE_ALL;
static bool active_power_thread = false;

int vitapower_thread(SceSize args, void *argp) {
  while (1) {
    if (!active_power_thread) {
      sceKernelDelayThread(10 * 1000 * 1000);
      continue;
    }
    if (powermode & DISABLE_SUSPEND) {
      sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
      sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
    }
    if (!scePowerIsBatteryCharging() && scePowerIsLowBattery()) {
      // TODO print warning message
    }
    sceKernelDelayThread(10 * 1000 * 1000);
  }

  return 0;
}

bool vitapower_init() {
  SceUID thid = sceKernelCreateThread("vitapower_thread", vitapower_thread, 0x10000100, 0x40000, 0, 0, NULL);
  if (thid >= 0)
    sceKernelStartThread(thid, 0, NULL);

  return true;
}

void vitapower_config(CONFIGURATION config) {
  powermode = ENABLE_ALL;

  if (config.disable_powersave) {
    powermode |= DISABLE_SUSPEND;
  }
}

/*
 * Clock boost: decode, render and network threads all run faster at the
 * maximum frequencies (same values as VitaRPS5 / PSVshell max, and
 * dubleyu/vita-moonlight-relay). The boost covers the stream, and also the
 * work that precedes it - reaching the PC, pairing, the list of games, the
 * stream start - whose encrypted exchanges are slow at the stock frequencies:
 * players reported Sunshine connection tests, pairing and stream starts timing
 * out (RTSP error 104) until they overclocked (xyzz/vita-moonlight #257, #269,
 * #284, #286; boost while connecting from juanjsebgarcia/vita-moonlight).
 * The system is also told that the application uses the wireless network
 * (scePowerSetUsingWireless), as dubleyu/vita-moonlight-relay does while
 * streaming. Everything is put back as it was when neither is running.
 */
static pthread_mutex_t clock_lock = PTHREAD_MUTEX_INITIALIZER;
static bool clocks_boosted = false;
static bool streaming = false;
static int busy_depth = 0;
static int saved_arm, saved_gpu, saved_bus, saved_xbar, saved_wireless;

static void boost_clocks() {
  if (clocks_boosted || !config.boost_clocks)
    return;
  saved_arm = scePowerGetArmClockFrequency();
  saved_gpu = scePowerGetGpuClockFrequency();
  saved_bus = scePowerGetBusClockFrequency();
  saved_xbar = scePowerGetGpuXbarClockFrequency();
  saved_wireless = scePowerGetUsingWireless();
  scePowerSetArmClockFrequency(444);
  scePowerSetGpuClockFrequency(222);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);
  int ret = scePowerSetUsingWireless(SCE_TRUE);
  if (ret < 0)
    vita_debug_log("scePowerSetUsingWireless: 0x%x\n", ret);
  clocks_boosted = true;
}

static void restore_clocks() {
  if (!clocks_boosted)
    return;
  scePowerSetArmClockFrequency(saved_arm);
  scePowerSetGpuClockFrequency(saved_gpu);
  scePowerSetBusClockFrequency(saved_bus);
  scePowerSetGpuXbarClockFrequency(saved_xbar);
  if (saved_wireless >= 0)
    scePowerSetUsingWireless(saved_wireless ? SCE_TRUE : SCE_FALSE);
  clocks_boosted = false;
}

static void update_clocks_locked(void) {
  if (streaming || busy_depth > 0)
    boost_clocks();
  else
    restore_clocks();
}

void vitapower_busy_begin(void) {
  pthread_mutex_lock(&clock_lock);
  busy_depth++;
  update_clocks_locked();
  pthread_mutex_unlock(&clock_lock);
}

void vitapower_busy_end(void) {
  pthread_mutex_lock(&clock_lock);
  if (busy_depth > 0)
    busy_depth--;
  update_clocks_locked();
  pthread_mutex_unlock(&clock_lock);
}

void vitapower_start() {
  active_power_thread = true;
  pthread_mutex_lock(&clock_lock);
  streaming = true;
  update_clocks_locked();
  pthread_mutex_unlock(&clock_lock);
}

void vitapower_stop() {
  active_power_thread = false;
  pthread_mutex_lock(&clock_lock);
  streaming = false;
  update_clocks_locked();
  pthread_mutex_unlock(&clock_lock);
}

int vitapower_wifi_power_save(void) {
  int value = -1;
  if (sceRegMgrGetKeyInt("/CONFIG/NET", "wifi_power_save", &value) < 0)
    return -1;
  return value;
}
