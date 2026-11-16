/*
 * UI preview: stand-ins for the Vita system calls the menus use, plus the
 * scripted input that drives a scenario (see preview_main.c).
 *
 * Time advances by one screen refresh per presented frame, the pad returns the
 * buttons of the current script step, and a step can save the frame as a PNG.
 */
#include <stdbool.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>
#include <psp2/power.h>
#include <psp2/display.h>
#include <psp2/videodec.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>

#include <cairo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "preview.h"

#define MAX_STEPS 4096

typedef struct {
  unsigned int buttons;
  int touch_x, touch_y;      /* -1: no touch */
  char snapshot[128];        /* file to write after this frame, or "" */
} step;

static step steps[MAX_STEPS];
static int step_count = 0;
static int frame = 0;
static char out_dir[512] = ".";

void preview_set_output(const char *dir) {
  snprintf(out_dir, sizeof(out_dir), "%s", dir);
}

static step *add_step(void) {
  if (step_count >= MAX_STEPS) {
    fprintf(stderr, "preview: script too long\n");
    exit(2);
  }
  step *s = &steps[step_count++];
  memset(s, 0, sizeof(*s));
  s->touch_x = s->touch_y = -1;
  return s;
}

/* Frames with no button pressed after the script, before the preview ends. */
static int idle_tail = 0;
void preview_idle_tail(int frames) {
  idle_tail = frames;
}

void preview_wait(int frames) {
  for (int i = 0; i < frames; i++)
    add_step();
}

/* One frame down, then enough frames up for the menu animations to settle. */
void preview_press(unsigned int buttons) {
  add_step()->buttons = buttons;
  preview_wait(14);
}

void preview_tap(int x, int y) {
  step *s = add_step();
  s->touch_x = x;
  s->touch_y = y;
  preview_wait(14);
}

void preview_snap(const char *name) {
  step *s = add_step();
  snprintf(s->snapshot, sizeof(s->snapshot), "%s", name);
}

static const step *current(void) {
  static const step idle = {0, -1, -1, ""};
  return frame < step_count ? &steps[frame] : &idle;
}

/* Virtual time: each frame is one refresh, each sceKernelDelayThread() moves it
 * on as well. A wait that sleeps for long without ever drawing is a hang. */
static SceUInt64 slept_us = 0, slept_since_frame_us = 0;
#define HANG_US (15ULL * 60 * 1000000)

/* What other threads do while the menu thread sleeps (journeys.c); none here. */
__attribute__((weak)) void preview_background(void) {}
/* What the menus show (src/gui/preview_hooks.h), followed by the journeys. */
__attribute__((weak)) void preview_on_screen(const char *name) {}
__attribute__((weak)) void preview_on_dialog(const char *kind, const char *text) {}

/* Buttons pressed for the journeys, on top of the script: one entry per frame
 * from the next one on (0 is a frame with the buttons released). */
#define INJECT_MAX 1024
static unsigned inject_queue[INJECT_MAX];
static int inject_head = 0, inject_count = 0, inject_frame = -1;

void preview_inject(unsigned buttons) {
  if (inject_count + 2 > INJECT_MAX) {
    fprintf(stderr, "preview: too many buttons queued\n");
    exit(2);
  }
  inject_queue[(inject_head + inject_count++) % INJECT_MAX] = buttons;
  inject_queue[(inject_head + inject_count++) % INJECT_MAX] = 0;
}

static bool inject_read = false;   /* the current entry was read during this frame */

static unsigned injected_now(void) {
  if (inject_count == 0)
    return 0;
  if (inject_frame < 0)
    inject_frame = frame + 1;    /* starts on the next frame */
  if (frame < inject_frame)
    return 0;
  inject_read = true;
  return inject_queue[inject_head];
}

/* An entry lasts until a frame has read it: none is lost on a screen that
 * does not read the buttons. */
static void inject_advance(void) {
  if (!inject_read)
    return;
  inject_read = false;
  inject_head = (inject_head + 1) % INJECT_MAX;
  inject_count--;
  inject_frame = inject_count ? frame + 1 : -1;
}

/* Called by the vita2d stand-in when a frame is presented. */
void preview_frame_done(cairo_surface_t *surface) {
  slept_since_frame_us = 0;
  inject_advance();
  preview_background();
  const step *s = current();
  if (s->snapshot[0]) {
    char path[700];
    snprintf(path, sizeof(path), "%s/%s.png", out_dir, s->snapshot);
    cairo_surface_write_to_png(surface, path);
  }
  frame++;
  if (frame >= step_count + idle_tail)
    exit(0);   /* script finished */
}

/* ---------------- system calls ---------------- */

int sceCtrlSetSamplingMode(SceCtrlPadInputMode mode) { return 0; }
int sceCtrlSetSamplingModeExt(SceCtrlPadInputMode mode) { return 0; }
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count) {
  memset(pad, 0, sizeof(*pad));
  pad->buttons = current()->buttons | injected_now();
  pad->lx = pad->ly = pad->rx = pad->ry = 0x80;
  return 1;
}

int sceTouchPeek(SceUInt32 port, SceTouchData *data, SceUInt32 count) {
  memset(data, 0, sizeof(*data));
  const step *s = current();
  if (port == SCE_TOUCH_PORT_FRONT && s->touch_x >= 0) {
    data->reportNum = 1;
    data->report[0].x = s->touch_x * 2;
    data->report[0].y = s->touch_y * 2;
  }
  return 1;
}
int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state) { return 0; }

SceUInt64 sceKernelGetProcessTimeWide(void) { return (SceUInt64)frame * 16683 + slept_us; }
int sceKernelDelayThread(SceUInt delay) {
  slept_us += delay;
  slept_since_frame_us += delay;
  if (slept_since_frame_us > HANG_US) {
    fprintf(stderr, "preview: the menu thread waited 15 minutes without drawing anything (hang)\n");
    exit(3);
  }
  preview_background();
  return 0;
}

int sceRtcGetCurrentClockLocalTime(SceDateTime *time) {
  memset(time, 0, sizeof(*time));
  time->year = 2026; time->month = 11; time->day = 15;
  time->hour = 21; time->minute = 42;
  return 0;
}
int sceRtcGetCurrentTick(SceRtcTick *tick) { tick->tick = (SceUInt64)frame * 16683; return 0; }

int scePowerGetBatteryLifePercent(void) { return 73; }
SceBool scePowerIsBatteryCharging(void) { return 0; }

int sceNetCtlInetGetState(int *state) { *state = SCE_NETCTL_STATE_CONNECTED; return 0; }

int sceNetCtlInetGetInfo(int code, SceNetCtlInfo *info) {
  memset(info, 0, sizeof(*info));
  info->rssi_percentage = 82;
  return 0;
}

int sceDisplayWaitVblankStart(void) { return 0; }

/* The hardware decoder accepts up to 1280x720 here, as on a real Vita. */
int sceVideodecInitLibrary(SceVideodecType type, const SceVideodecQueryInitInfoHwAvcdec *info) {
  return info->horizontal <= 1280 && info->vertical <= 720 ? 0 : -1;
}
int sceVideodecTermLibrary(SceVideodecType type) { return 0; }

int sceClibPrintf(const char *fmt, ...) { return 0; }

/* The log (src/log.c) is not part of the preview: lines go nowhere. */
#include "log.h"
void log_write(log_level level, const char *tag, const char *fmt, ...) {}
void log_vwrite(log_level level, const char *tag, const char *fmt, va_list va) {}
void log_set_detailed(bool on) {}
bool log_detailed(void) { return false; }
void log_flush(void) {}
void log_system_state(const char *when) {}
void log_mcc(const char *fmt, ...) {}
