/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Box art download and cache; see boxart.h.
 */
#include "boxart.h"
#include "cover_shape.h"
#include "../config.h"
#include "../debug.h"
#include "../connection.h"
#include "errors.h"

#include <png.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JOBS 128
#define COVER_MAGIC "MLBA"

typedef struct {
  int id;
  char name[64];
} boxart_job;

static boxart_job jobs[MAX_JOBS];
static int job_count = 0;
static SERVER_DATA job_server;
static char job_address[256];   /* own copy: the worker outlives the caller's strings */
/* A pthread: it goes through the HTTP client, whose lock is a pthread mutex
 * (dubleyu/vita-moonlight-relay saw pthread waits crash in kernel threads). */
static pthread_t worker;
static bool worker_created = false;
static volatile bool worker_busy = false;

/* <key_dir>/boxart/<host>, where <host> is the MAC (or the address) with only
 * filename-safe characters. */
static void host_dir(PSERVER_DATA server, char *out, size_t len) {
  const char *id = server->mac[0] ? server->mac : server->serverInfo.address;
  char safe[64];
  size_t j = 0;
  for (size_t i = 0; id && id[i] && j < sizeof(safe) - 1; i++) {
    char c = id[i];
    safe[j++] = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) ? c : '-';
  }
  safe[j] = 0;
  snprintf(out, len, "%.400s/boxart/%s", config.key_dir, j ? safe : "host");
}

static void cover_path(PSERVER_DATA server, int app_id, char *out, size_t len) {
  char dir[512];
  host_dir(server, dir, sizeof(dir));
  snprintf(out, len, "%s/%d.cover", dir, app_id);
}

static bool file_exists(const char *path) {
  SceIoStat st;
  return sceIoGetstat(path, &st) >= 0;
}

/* PNG bytes -> BOXART_W x BOXART_H RGBA, cropped to fill (like CSS object-fit: cover)
 * and downscaled by averaging each destination pixel's source area. */
static uint8_t *decode_and_fit(const char *png, size_t size, unsigned *src_w, unsigned *src_h) {
  png_image img;
  memset(&img, 0, sizeof(img));
  img.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&img, png, size))
    return NULL;
  img.format = PNG_FORMAT_RGBA;
  uint8_t *src = malloc(PNG_IMAGE_SIZE(img));
  if (!src) {
    png_image_free(&img);
    return NULL;
  }
  if (!png_image_finish_read(&img, NULL, src, 0, NULL)) {
    free(src);
    return NULL;
  }
  unsigned w = img.width, h = img.height;
  *src_w = w;
  *src_h = h;

  /* Source rectangle with the destination aspect ratio, centred. */
  double scale_x = (double)w / BOXART_W, scale_y = (double)h / BOXART_H;
  double s = scale_x < scale_y ? scale_x : scale_y;
  double crop_w = BOXART_W * s, crop_h = BOXART_H * s;
  double off_x = (w - crop_w) / 2.0, off_y = (h - crop_h) / 2.0;

  uint8_t *dst = malloc(BOXART_W * BOXART_H * 4);
  if (!dst) {
    free(src);
    return NULL;
  }
  for (int y = 0; y < BOXART_H; y++) {
    unsigned y0 = (unsigned)(off_y + y * s), y1 = (unsigned)(off_y + (y + 1) * s);
    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > h) y1 = h;
    for (int x = 0; x < BOXART_W; x++) {
      unsigned x0 = (unsigned)(off_x + x * s), x1 = (unsigned)(off_x + (x + 1) * s);
      if (x1 <= x0) x1 = x0 + 1;
      if (x1 > w) x1 = w;
      uint32_t acc[4] = {0, 0, 0, 0}, n = 0;
      for (unsigned sy = y0; sy < y1; sy++) {
        const uint8_t *p = src + ((size_t)sy * w + x0) * 4;
        for (unsigned sx = x0; sx < x1; sx++, p += 4) {
          acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3];
          n++;
        }
      }
      uint8_t *d = dst + ((size_t)y * BOXART_W + x) * 4;
      for (int k = 0; k < 4; k++)
        d[k] = n ? (uint8_t)(acc[k] / n) : 0;
    }
  }
  free(src);
  return dst;
}

static bool write_cover(const char *path, const uint8_t *rgba) {
  SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0)
    return false;
  uint16_t dims[2] = {BOXART_W, BOXART_H};
  bool ok = sceIoWrite(fd, COVER_MAGIC, 4) == 4 &&
            sceIoWrite(fd, dims, sizeof(dims)) == sizeof(dims) &&
            sceIoWrite(fd, rgba, BOXART_W * BOXART_H * 4) == BOXART_W * BOXART_H * 4;
  sceIoClose(fd);
  if (!ok)
    sceIoRemove(path);
  return ok;
}

static void *worker_main(void *argp) {
  // Below the menus (priority 160, larger is lower): covers come second.
  sceKernelChangeThreadPriority(sceKernelGetThreadId(), 176);
  char dir[512], path[640], line[256];
  host_dir(&job_server, dir, sizeof(dir));
  snprintf(path, sizeof(path), "%.400s/boxart", config.key_dir);
  sceIoMkdir(path, 0777);
  sceIoMkdir(dir, 0777);

  snprintf(path, sizeof(path), "%s/index.txt", dir);
  SceUID index = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);

  for (int i = 0; i < job_count; i++) {
    cover_path(&job_server, jobs[i].id, path, sizeof(path));
    if (file_exists(path))
      continue;

    /* Never compete with a running stream for Wi-Fi time and CPU. */
    while (connection_get_status() == LI_CONNECTED)
      sceKernelDelayThread(500000);

    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    char *png = NULL;
    size_t size = 0;
    unsigned w = 0, h = 0;
    const char *status = "ok";
    int ret = gs_app_boxart(&job_server, jobs[i].id, &png, &size);
    if (ret != GS_OK) {
      status = ret == GS_INVALID ? "not-a-png" : "download-failed";
    } else {
      uint8_t *rgba = decode_and_fit(png, size, &w, &h);
      if (!rgba)
        status = "decode-failed";
      else if (!write_cover(path, rgba))
        status = "write-failed";
      free(rgba);
    }
    free(png);

    unsigned ms = (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000);
    snprintf(line, sizeof(line), "%d;%s;%s;%ux%u;%u bytes;%u ms\n",
             jobs[i].id, jobs[i].name, status, w, h, (unsigned)size, ms);
    if (index >= 0)
      sceIoWrite(index, line, strlen(line));
    vita_debug_log("boxart %s", line);

    sceKernelDelayThread(20000); /* leave the HTTP client to the UI between covers */
  }
  if (index >= 0)
    sceIoClose(index);
  worker_busy = false;
  return NULL;
}

void boxart_prefetch(PSERVER_DATA server, PAPP_LIST apps) {
  if (worker_busy || server == NULL)
    return;

  if (worker_created) {
    /* previous run finished (worker_busy is false): reap it */
    pthread_join(worker, NULL);
    worker_created = false;
  }

  /* Copy what the worker needs: the UI may free or reload the list meanwhile. */
  job_server = *server;
  snprintf(job_address, sizeof(job_address), "%s", server->serverInfo.address ? server->serverInfo.address : "");
  job_server.serverInfo.address = job_address;
  job_count = 0;
  for (PAPP_LIST a = apps; a != NULL && job_count < MAX_JOBS; a = a->next) {
    jobs[job_count].id = a->id;
    snprintf(jobs[job_count].name, sizeof(jobs[job_count].name), "%s", a->name ? a->name : "");
    job_count++;
  }
  if (job_count == 0)
    return;

  worker_busy = true;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 0x20000);
  int err = pthread_create(&worker, &attr, worker_main, NULL);
  pthread_attr_destroy(&attr);
  if (err != 0) {
    worker_busy = false;
    return;
  }
  worker_created = true;
}

void boxart_cover_dir(PSERVER_DATA server, char *out, size_t len) {
  host_dir(server, out, len);
}

vita2d_texture *boxart_load_texture(PSERVER_DATA server, int app_id) {
  char dir[512];
  host_dir(server, dir, sizeof(dir));
  return boxart_load_texture_from(dir, app_id);
}

vita2d_texture *boxart_load_texture_from(const char *dir, int app_id) {
  char path[640];
  snprintf(path, sizeof(path), "%.500s/%d.cover", dir, app_id);
  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (fd < 0)
    return NULL;

  char magic[4];
  uint16_t dims[2];
  vita2d_texture *tex = NULL;
  if (sceIoRead(fd, magic, 4) == 4 && memcmp(magic, COVER_MAGIC, 4) == 0 &&
      sceIoRead(fd, dims, sizeof(dims)) == sizeof(dims) && dims[0] == BOXART_W && dims[1] == BOXART_H) {
    tex = vita2d_create_empty_texture(BOXART_W, BOXART_H);
    if (tex) {
      /* covers are drawn scaled: filter them */
      vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
      uint8_t *data = vita2d_texture_get_datap(tex);
      unsigned stride = vita2d_texture_get_stride(tex);
      for (int y = 0; y < BOXART_H && tex; y++) {
        if (sceIoRead(fd, data + (size_t)y * stride, BOXART_W * 4) != BOXART_W * 4) {
          vita2d_free_texture(tex);
          tex = NULL;
        }
      }
      if (tex)
        cover_round_corners(data, BOXART_W, BOXART_H, stride, COVER_RADIUS_TEX);
    }
  }
  sceIoClose(fd);
  return tex;
}
