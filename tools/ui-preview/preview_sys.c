/*
 * UI preview: stand-ins for the Vita system calls the menus use, plus the
 * scripted input that drives a scenario (see preview_main.c).
 *
 * Time advances by one screen refresh per presented frame, the pad returns the
 * buttons of the current script step, and a step can save the frame as a PNG.
 */
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

/* Called by the vita2d stand-in when a frame is presented. */
void preview_frame_done(cairo_surface_t *surface) {
  const step *s = current();
  if (s->snapshot[0]) {
    char path[700];
    snprintf(path, sizeof(path), "%s/%s.png", out_dir, s->snapshot);
    cairo_surface_write_to_png(surface, path);
  }
  frame++;
  if (frame >= step_count)
    exit(0);   /* script finished */
}

/* ---------------- system calls ---------------- */

int sceCtrlSetSamplingMode(SceCtrlPadInputMode mode) { return 0; }
int sceCtrlSetSamplingModeExt(SceCtrlPadInputMode mode) { return 0; }
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count) {
  memset(pad, 0, sizeof(*pad));
  pad->buttons = current()->buttons;
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

SceUInt64 sceKernelGetProcessTimeWide(void) { return (SceUInt64)frame * 16683; }
int sceKernelDelayThread(SceUInt delay) { return 0; }

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
