#include <stdbool.h>
#include <time.h>
#include "../config.h"
#include "psp2/motion.h"
#include "Limelight.h"
#include "motion.h"
#include "../debug.h"
#include "vita.h"

#include "psp2/power.h"
#include "../thread_policy.h"

#include <pthread.h>
#include <psp2/kernel/threadmgr.h>



// TODO: Config Value
static uint32_t MOTION_SCALE = 28;

bool active_motion_threads = false;

motion_data_state motion_state = {
    .motion_type_gyro_enabled = 0,
    .motion_type_accel_enabled = 0,
    .report_rate_gyro = 1,
    .report_rate_accel = 1,
};

/* The motion threads send through moonlight-common-c, whose locks are pthread
 * ones: they are pthreads (dubleyu/vita-moonlight-relay saw crashes in
 * sem_timedwait when kernel threads called in). */
static bool start_motion_thread(void *(*entry)(void *)) {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x40000);
    int err = pthread_create(&thread, &attr, entry, NULL);
    pthread_attr_destroy(&attr);
    if (err != 0)
        return false;
    pthread_detach(thread);
    return true;
}

bool vita_motion_init() {

    sceMotionStartSampling();
    sceMotionReset();

    return start_motion_thread(vitainput_motion_gyro_thread) &&
           start_motion_thread(vitainput_motion_accel_thread);
}

/* Delay between two reports for a rate asked by the host. Bounded so that an
 * unexpected rate (0, or above 500 Hz) can neither divide by zero nor turn the
 * thread into a busy loop. */
static SceUInt motion_period_us(uint32_t rate_hz) {
  if (rate_hz == 0)
    return 1000000;
  SceUInt period = 1000000 / rate_hz;
  return period < 2000 ? 2000 : period;
}

void *vitainput_motion_gyro_thread(void *argp) {
  bool placed = false;
  while (1) {
    if (motion_state.motion_type_gyro_enabled == true && active_motion_threads == true) {
      if (!placed) {
        // The configuration is loaded after this thread starts: place it once
        // a stream is running.
        thread_place(0, CORE_UI);
        placed = true;
      }
      motion_process_gyro();

      //TODO: Reporting battery events more than one time per second seems unwise
      //Possible solutions: New thread (heavy handed), using main thread (Idk)

      /* uint32_t battery_percent = scePowerGetBatteryLifePercent();
      
      uint8_t battery_state;
      if (scePowerIsBatteryCharging()) {
        battery_state = LI_BATTERY_STATE_CHARGING;
      } else {
        battery_state = LI_BATTERY_STATE_DISCHARGING;
      }

      LiSendControllerBatteryEvent(0, battery_state, battery_percent); */

    } else {
      placed = false;
      sceKernelDelayThread(1000000);
    }
    sceKernelDelayThread(motion_period_us(motion_state.report_rate_gyro));
  }
  
  return NULL;
}

void *vitainput_motion_accel_thread(void *argp) {
  bool placed = false;
  while (1) {
    if (motion_state.motion_type_accel_enabled == true && active_motion_threads == true) {
      if (!placed) {
        thread_place(0, CORE_UI);
        placed = true;
      }
      motion_process_accel();
    } else {
      placed = false;
      sceKernelDelayThread(1000000);
    }

    sceKernelDelayThread(motion_period_us(motion_state.report_rate_accel));
  }

  return NULL;
}

void motion_process_gyro() {
  SceMotionState motionState;
  sceMotionGetState(&motionState);

  //Angular velocity should be in deg/s

  float vx = motionState.angularVelocity.x;
  float vy = motionState.angularVelocity.z;
  float vz = motionState.angularVelocity.y * -1;

  vx = vx * MOTION_SCALE;
  vy = vy * MOTION_SCALE;
  vz = vz * MOTION_SCALE;

  //vita_debug_log("Gyro: vx: %f vy: %f vz: %f", vx, vy, vz);
  LiSendControllerMotionEvent(0, LI_MOTION_TYPE_GYRO, vx, vy, vz);
}

void motion_process_accel() {
  SceMotionState motionState;
  sceMotionGetState(&motionState);

  float vx = motionState.acceleration.x;
  float vy = motionState.acceleration.z;
  float vz = motionState.acceleration.y * -1;

  vx = vx * MOTION_SCALE;
  vy = vy * MOTION_SCALE;
  vz = vz * MOTION_SCALE;

  //vita_debug_log("Accel: vx: %f\nvy: %f\nvz: %f\n", vx, vy, vz);
  LiSendControllerMotionEvent(0, LI_MOTION_TYPE_ACCEL, vx, vy, vz);

}