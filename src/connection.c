
/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
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
#include "Limelight.h"
#include "config.h"
#include "power/vita.h"
#include "input/vita.h"
#include "video/vita.h"
#include "video/stream_stats.h"
#include "audio/vita.h"
#include <stdbool.h>
#include <pthread.h>
#include "connection_overlay.h"
#include "debug.h"
#include "log.h"
#include <psp2/kernel/processmgr.h>
#include "reconnect.h"

static int connection_status = LI_DISCONNECTED;

/* How the last stream ended (connection_was_lost()). Written under
 * terminate_lock, read by the menus once the status is LI_DISCONNECTED. */
static volatile bool stream_lost = false;
static volatile int last_error = 0;

extern motion_data_state motion_state;

int connection_stage = 0;

bool pause_overlay_is_open(void) {
    return connection_status == LI_MINIMIZED;
}

void pause_output() {
  vitainput_stop();
  vitavideo_stop();
  vitaaudio_stop();
}

void stop_output() {
  vitainput_stop();
  vitapower_stop();
  vitavideo_stop();
  vitaaudio_stop();
}

void start_output() {
  vitainput_start();
  vitapower_start();
  vitavideo_start();
  vitaaudio_start();
}

/* When the stream started, for its duration in the log. */
static SceUInt64 stream_started_us;

const char *connection_error_name(int error_code) {
  switch (error_code) {
    case ML_ERROR_GRACEFUL_TERMINATION: return "ended normally";
    case ML_ERROR_NO_VIDEO_TRAFFIC: return "no video received (firewall?)";
    case ML_ERROR_NO_VIDEO_FRAME: return "no complete video frame (network too weak)";
    case ML_ERROR_UNEXPECTED_EARLY_TERMINATION: return "ended by the PC early (capture error)";
    case ML_ERROR_PROTECTED_CONTENT: return "ended by the PC (protected content)";
    case ML_ERROR_FRAME_CONVERSION: return "ended by the PC (picture conversion error)";
    default: return "connection lost or failed";
  }
}

void connection_connection_started() {
  if (connection_status != LI_PAIRED) {
    LOGW("stream", "started in an unexpected state (%d)", connection_status);
    return;
  }
  stream_started_us = sceKernelGetProcessTimeWide();
  LOGI("stream", "running");
  stream_lost = false;
  connection_status = LI_CONNECTED;
  start_output();
  vitavideo_hide_poor_net_indicator();
}

/* The end of a stream can come from two threads at once: moonlight-common-c
 * reports a host-side end on its own thread ("AsyncTerm") while the menus may
 * be ending it too. LiStopConnection() must run once, so both go through this
 * lock and the second finds the stream already ended. */
static pthread_mutex_t terminate_lock = PTHREAD_MUTEX_INITIALIZER;

static void connection_connection_terminated(int error_code) {
  pthread_mutex_lock(&terminate_lock);
  if (connection_status != LI_PAIRED && connection_status != LI_CONNECTED &&
      connection_status != LI_MINIMIZED) {
    vita_debug_log("connection_connection_terminated: already ended (%d)\n", connection_status);
    pthread_mutex_unlock(&terminate_lock);
    return;
  }

  unsigned ran_s = stream_started_us && connection_status != LI_PAIRED
                     ? (unsigned)((sceKernelGetProcessTimeWide() - stream_started_us) / 1000000ULL) : 0;
  log_write(error_code == ML_ERROR_GRACEFUL_TERMINATION ? LOG_INFO : LOG_WARN, "stream",
            "ended: %s (code %d) after %u s, %s", connection_error_name(error_code), error_code, ran_s,
            connection_status == LI_MINIMIZED ? "while paused" : connection_status == LI_PAIRED ? "while starting" : "while playing");

  LiStopConnection();

  last_error = error_code;
  // Only a running stream is reconnected: one lost during the pause is left to
  // the menus, where the game can be resumed by hand.
  stream_lost = connection_status == LI_CONNECTED && reconnect_after_error(error_code);

  if (connection_status == LI_CONNECTED) {
    stop_output();
  } else if (connection_status == LI_MINIMIZED) {
    // pause_output() leaves the clocks raised: a stream ended from the pause
    // must still put them back (juanjsebgarcia/vita-moonlight).
    vitapower_stop();
  }
  connection_status = LI_DISCONNECTED;
  stream_started_us = 0;
  pthread_mutex_unlock(&terminate_lock);
  log_system_state("after the stream");
  log_flush();
}

int connection_reset() {
  if (connection_status != LI_DISCONNECTED) {
    vita_debug_log("connection_reset error: %d\n", connection_status);
    return -1;
  }
  connection_status = LI_READY;
  stream_lost = false;
  return 0;
}

bool connection_was_lost() {
  return stream_lost;
}

int connection_last_error() {
  return last_error;
}

int connection_paired() {
  if (connection_status != LI_READY && connection_status != LI_PAIRED &&
      connection_status != LI_CONNECTED) {
    vita_debug_log("connection_paired error: %d\n", connection_status);
    return -1;
  }
  connection_status = LI_PAIRED;
  return 0;
}

int connection_minimize() {
  if (connection_status != LI_CONNECTED) {
    vita_debug_log("connection_minimize error: %d\n", connection_status);
    return -1;
  }
  pause_output();
  connection_status = LI_MINIMIZED;
  LOGI("stream", "paused");
  return 0;
}

int connection_resume() {
  if (connection_status != LI_MINIMIZED) {
    vita_debug_log("connection_resume error: %d\n", connection_status);
    return -1;
  }
  start_output();
  connection_status = LI_CONNECTED;
  LOGI("stream", "resumed");
  return 0;
}

int connection_terminate() {
  if (connection_status != LI_PAIRED && connection_status != LI_CONNECTED &&
      connection_status != LI_MINIMIZED) {
    vita_debug_log("connection_terminate error: %d\n", connection_status);
    return -1;
  }
  connection_connection_terminated(0);
  return 0;
}

/* Forgets the session with the PC when no stream is running or paused: only the
 * status is reset, nothing is open in these states. */
void connection_close_idle() {
  if (connection_status == LI_READY || connection_status == LI_PAIRED)
    connection_status = LI_DISCONNECTED;
}

/* Stages of the stream start (RTSP, control, video, audio...), each timed. */
static SceUInt64 stage_started_us;

void connection_stage_starting(int stage) {
  connection_stage = stage;
  stage_started_us = sceKernelGetProcessTimeWide();
  LOGD("stream", "stage %s...", LiGetStageName(stage));
}

void connection_stage_complate(int stage) {
  connection_stage = stage;
  LOGI("stream", "stage %s: done in %u ms", LiGetStageName(stage),
       (unsigned)((sceKernelGetProcessTimeWide() - stage_started_us) / 1000));
}

void connection_stage_failed(int stage, int code) {
  connection_stage = stage;
  LOGE("stream", "stage %s failed after %u ms: error %d", LiGetStageName(stage),
       (unsigned)((sceKernelGetProcessTimeWide() - stage_started_us) / 1000), code);
}

bool connection_is_ready() {
  return connection_status != LI_DISCONNECTED;
}

bool connection_is_connected() {
  return connection_status == LI_CONNECTED;
}

int connection_get_status() {
  return connection_status;
}

void connection_status_update(int status) {
  stream_stats_on_conn_status(status == CONN_STATUS_POOR);
  LOGI("net", "connection quality: %s", status == CONN_STATUS_POOR ? "poor" : "okay");
  switch (status) {
    case CONN_STATUS_POOR:
      vitavideo_show_poor_net_indicator();
      break;
    case CONN_STATUS_OKAY:
      vitavideo_hide_poor_net_indicator();
      break;
  }
}

void connection_set_motion_state(uint16_t controller, uint8_t motion_type, uint16_t report_rate) {
  LOGI("input", "the PC asks for %s at %u Hz (controller %u)%s",
       motion_type == LI_MOTION_TYPE_GYRO ? "the gyroscope" : "the accelerometer", report_rate, controller,
       config.enable_motion_controls ? "" : ": motion sensors are off in the settings");

  //TODO: Multicontroller support here someday? Can't afford pstv tho
  if (config.enable_motion_controls == false)
    return;

  if (report_rate == 0) {
    switch (motion_type) {
      case LI_MOTION_TYPE_GYRO:
        motion_state.motion_type_gyro_enabled = false;
        motion_state.report_rate_gyro = 1;
        break;
      case LI_MOTION_TYPE_ACCEL:
        motion_state.motion_type_accel_enabled = false;
        motion_state.report_rate_accel = 1;
        break;
    }
  } else {

    switch (motion_type) {
      case LI_MOTION_TYPE_GYRO:
        motion_state.motion_type_gyro_enabled = true;
        motion_state.report_rate_gyro = report_rate;
        vita_debug_log("Setting gyro state true");
        break;
      case LI_MOTION_TYPE_ACCEL:
        motion_state.motion_type_accel_enabled = true;
        motion_state.report_rate_accel = report_rate;
        vita_debug_log("Setting accel state true");
        break;
    }
  }
}

CONNECTION_LISTENER_CALLBACKS connection_callbacks = {
  .stageStarting = connection_stage_starting,
  .stageComplete = connection_stage_complate,
  .stageFailed = connection_stage_failed,
  .connectionStarted = connection_connection_started,
  .connectionTerminated = connection_connection_terminated,
  .connectionStatusUpdate = connection_status_update,
  .logMessage = log_mcc,
  .setMotionEventState = connection_set_motion_state
};
