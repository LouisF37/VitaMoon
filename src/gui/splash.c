/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Start-up screen: see splash.h. VitaRPS5 has the same idea (its logo, made of
 * particles, over its background while it loads); this one is VitaMoon's own:
 * the moon goes through its phases and ends as the mark, which takes its place
 * in the top bar.
 */
#include "splash.h"
#include "splash_logic.h"
#include "guilib.h"
#include "theme.h"
#include "preview_hooks.h"
#include "../app_info.h"
#include "../log.h"

#include <stdio.h>
#include <string.h>

#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#define SPLASH_FRAME_US    16000    /* a frame between steps when one is due */
#define SPLASH_GLYPHS_US   20000    /* glyphs rendered per frame, at most */

/* Weights of the menus' own loading (the steps of main.c come in between). */
#define WEIGHT_FONTS   2
#define WEIGHT_ICONS   1
#define WEIGHT_GLYPHS 12

#define TIMINGS_MAX 16

static splash_moon moon;
static float progress = 0.0f;
static bool loaded = false;
static SceUInt64 name_ready_us = 0;
static SceUInt64 last_frame_us = 0;
static unsigned int frames = 0;
static bool intro_pending = false;

static struct {
  const char *name;
  unsigned int ms;
} timings[TIMINGS_MAX];
static int timing_count = 0;

static void note(const char *name, SceUInt64 start_us) {
  if (timing_count < TIMINGS_MAX) {
    timings[timing_count].name = name;
    timings[timing_count].ms = (unsigned int)((sceKernelGetProcessTimeWide() - start_us + 500) / 1000);
    timing_count++;
  }
}

/* The steps of main.c (network, controls, PCs...) draw nothing: they run on a
 * thread of their own while this one renders the fonts and moves the moon, so
 * the two loads overlap (the Vita has three cores) and a step that blocks for
 * a while does not stop the animation. */
static struct {
  const splash_step *steps;
  int count;
  volatile int done_weight;
  volatile int finished;
  const char *volatile error;
  const char *failed_step;
  unsigned int failed_ms;
  struct {
    const char *name;
    unsigned int ms;
  } timings[TIMINGS_MAX];
  int timing_count;
} worker;

static int worker_main(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  for (int i = 0; i < worker.count; i++) {
    SceUInt64 t = sceKernelGetProcessTimeWide();
    const char *error = worker.steps[i].run();
    unsigned int ms = (unsigned int)((sceKernelGetProcessTimeWide() - t + 500) / 1000);
    if (worker.timing_count < TIMINGS_MAX) {
      worker.timings[worker.timing_count].name = worker.steps[i].name;
      worker.timings[worker.timing_count].ms = ms;
      worker.timing_count++;
    }
    if (error) {
      worker.failed_step = worker.steps[i].name;
      worker.failed_ms = ms;
      worker.error = error;
      break;
    }
    worker.done_weight += worker.steps[i].weight;
  }
  __sync_synchronize();   /* what is above is seen before `finished` */
  worker.finished = 1;
  return 0;
}

static void draw_frame(void) {
  SceUInt64 now = sceKernelGetProcessTimeWide();
  splash_moon_update(&moon, splash_target(progress, loaded), loaded, now);
  float glide = splash_glide(&moon, now);
  float name_glide = splash_name_glide(splash_glide_time(&moon, now));

  ui_start();   /* the theme's sky, as on Home */
  if (glide > 0.0f) {
    /* the rest of the top bar comes in while the mark and the name go there */
    theme_set_alpha(glide);
    theme_topbar_status();
    theme_set_alpha(1.0f);
  }

  splash_place mp = splash_moon_place(glide, (float)MOON_CELL, SPLASH_MOON_CX, SPLASH_MOON_CY,
                                      (float)TH_MARK_X, (float)TH_MARK_Y, TH_MARK_SIZE);
  float shine = splash_earthshine(moon.shown);
  if (shine > 0.0f)
    guilib_draw_moon(MOON_PHASES - 2, mp.x, mp.y, mp.scale, shine);
  splash_pictures pics = splash_pictures_for(moon.shown);
  guilib_draw_moon(pics.base, mp.x, mp.y, mp.scale, 1.0f);
  if (pics.over >= 0)
    guilib_draw_moon(pics.over, mp.x, mp.y, mp.scale, pics.over_alpha);

  float name_alpha = splash_name_alpha(name_ready_us, now);
  if (name_alpha > 0.0f) {
    float width = (float)theme_text_width(TS_HERO, APP_NAME);
    float bar_x = (float)(theme_has_mark() ? TH_NAME_X : TH_MARGIN);
    splash_place np = splash_name_place(name_glide, SPLASH_MOON_CX, SPLASH_NAME_BASE, (float)TS_HERO, width,
                                        bar_x, (float)TH_TOPBAR_BASE, (float)TS_LEAD);
    float rise = (1.0f - name_alpha) * SPLASH_NAME_RISE * (1.0f - name_glide);
    theme_set_alpha(name_alpha);
    theme_text_scaled(np.x, np.y + rise, TH_TEXT, TS_HERO, np.scale, APP_NAME);
    theme_set_alpha(1.0f);
  }

  /* Buttons pressed now are not presses on Home: their state is kept up to date. */
  read_buttons();
  ui_end();
  last_frame_us = now;
  frames++;
}

static void frame_if_due(void) {
  if (sceKernelGetProcessTimeWide() - last_frame_us >= SPLASH_FRAME_US)
    draw_frame();
}

const char *splash_run(const splash_step *steps, int count) {
  PREVIEW_SCREEN("splash");
  SceUInt64 start = sceKernelGetProcessTimeWide();
  memset(&moon, 0, sizeof(moon));
  memset(&worker, 0, sizeof(worker));
  worker.steps = steps;
  worker.count = count;
  timing_count = 0;
  frames = 0;

  guilib_init_display();      /* vita2d, the theme's sky, the mark */
  draw_frame();
  SceUInt64 first_frame = sceKernelGetProcessTimeWide();
  note("display", start);

  int steps_weight = 0;
  for (int i = 0; i < count; i++)
    steps_weight += steps[i].weight;
  int total = WEIGHT_FONTS + WEIGHT_ICONS + WEIGHT_GLYPHS + steps_weight;
  int done = 0;   /* this thread's part */

  SceUID thread = sceKernelCreateThread("startup", worker_main, 0x10000100, 0x40000, 0, 0, NULL);
  if (thread >= 0 && sceKernelStartThread(thread, 0, NULL) < 0) {
    sceKernelDeleteThread(thread);
    thread = -1;
  }
  if (thread < 0) {
    LOGW("app", "start-up thread not available (0x%08X): steps run one after the other", (unsigned int)thread);
    worker_main(0, NULL);
  }

  /* The fonts first, so that the name shows early. */
  SceUInt64 t = sceKernelGetProcessTimeWide();
  theme_load_fonts();
  theme_prepare_text(TS_HERO, APP_NAME);
  theme_prepare_text(TS_LEAD, APP_NAME);
  name_ready_us = sceKernelGetProcessTimeWide();
  note("fonts", t);
  done += WEIGHT_FONTS;
  progress = splash_progress(done + worker.done_weight, 0, 0.0f, total);
  draw_frame();

  t = sceKernelGetProcessTimeWide();
  guilib_load_icons();
  note("icons", t);
  done += WEIGHT_ICONS;
  progress = splash_progress(done + worker.done_weight, 0, 0.0f, total);
  frame_if_due();

  /* Every character in every size, a slice per frame so that the moon moves on. */
  t = sceKernelGetProcessTimeWide();
  float part = 0.0f;
  while (!worker.error && !theme_prewarm(SPLASH_GLYPHS_US, &part)) {
    progress = splash_progress(done + worker.done_weight, WEIGHT_GLYPHS, part, total);
    draw_frame();
  }
  note("glyphs", t);
  done += WEIGHT_GLYPHS;

  /* The other thread's steps, if they are not done yet. */
  t = sceKernelGetProcessTimeWide();
  while (!worker.finished) {
    progress = splash_progress(done + worker.done_weight, 0, 0.0f, total);
    draw_frame();
  }
  __sync_synchronize();
  if (thread >= 0) {
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    sceKernelDeleteThread(thread);
  }
  note("waiting for steps", t);
  for (int i = 0; i < worker.timing_count && timing_count < TIMINGS_MAX; i++) {
    timings[timing_count].name = worker.timings[i].name;
    timings[timing_count].ms = worker.timings[i].ms;
    timing_count++;
  }
  if (worker.error) {
    LOGE("app", "start-up: %s failed after %u ms: %s", worker.failed_step, worker.failed_ms, worker.error);
    return worker.error;
  }
  progress = 1.0f;

  SceUInt64 loaded_at = sceKernelGetProcessTimeWide();
  loaded = true;
  while (!splash_finished(&moon, sceKernelGetProcessTimeWide()))
    draw_frame();
  SceUInt64 end = sceKernelGetProcessTimeWide();

  char line[512];
  int n = snprintf(line, sizeof(line), "start-up: first frame %u ms after launch;",
                   (unsigned int)(first_frame / 1000));
  for (int i = 0; i < timing_count && n > 0 && n < (int)sizeof(line); i++)
    n += snprintf(line + n, sizeof(line) - n, " %s %u ms%s", timings[i].name, timings[i].ms,
                  i + 1 < timing_count ? "," : ";");
  if (n > 0 && n < (int)sizeof(line))
    snprintf(line + n, sizeof(line) - n, " loaded at %u ms, Home at %u ms after launch, %u frames",
             (unsigned int)(loaded_at / 1000), (unsigned int)(end / 1000), frames);
  LOGI("app", "%s", line);

  read_buttons();
  intro_pending = true;
  return NULL;
}

void splash_abort(void) {
  if (!guilib_display_ready())
    return;
  vita2d_wait_rendering_done();
  for (int i = 0; i < 3; i++)
    sceDisplayWaitVblankStart();
}

bool splash_take_intro(void) {
  bool intro = intro_pending;
  intro_pending = false;
  return intro;
}
