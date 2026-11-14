/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2016 Ilya Zhuravlev
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

#include "../video.h"
#include "../config.h"
#include "../debug.h"
#include "../gui/guilib.h"
#include "../util.h"
#include "../input/vita.h"
#include "vita.h"
#include "sps.h"
#include "stream_stats.h"

#include <Limelight.h>

#include <stdbool.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/display.h>
#include <psp2/videodec.h>
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define printf vita_debug_log

extern void gs_sps_stop();

extern double_click_tracker dc_tracker;

void draw_streaming(vita2d_texture *frame_texture);
void draw_fps();
void draw_indicators();

enum {
  VITA_VIDEO_INIT_OK                    = 0,
  VITA_VIDEO_ERROR_NO_MEM               = 0x80010001,
  VITA_VIDEO_ERROR_INIT_LIB             = 0x80010002,
  VITA_VIDEO_ERROR_QUERY_DEC_MEMSIZE    = 0x80010003,
  VITA_VIDEO_ERROR_ALLOC_MEM            = 0x80010004,
  VITA_VIDEO_ERROR_GET_MEMBASE          = 0x80010005,
  VITA_VIDEO_ERROR_CREATE_DEC           = 0x80010006,
  VITA_VIDEO_ERROR_CREATE_PACER_THREAD  = 0x80010007,
};

#define DECODER_BUFFER_SIZE (128 * 1024)

#define AV_INPUT_BUFFER_PADDING_SIZE 64

static char* decoder_buffer = NULL;

static size_t decoder_buffer_size = 0;

enum {
  SCREEN_WIDTH = 960,
  SCREEN_HEIGHT = 544,
  LINE_SIZE = 960,
  FRAMEBUFFER_SIZE = 2 * 1024 * 1024,
  FRAMEBUFFER_ALIGNMENT = 256 * 1024
};

enum VideoStatus {
  NOT_INIT,
  INIT_GS,
  INIT_FRAMEBUFFER,
  INIT_AVC_LIB,
  INIT_DECODER_MEMBLOCK,
  INIT_AVC_DEC,
  INIT_FRAME_PACER_THREAD,
};

vita2d_texture *frame_texture = NULL;
enum VideoStatus video_status = NOT_INIT;

/*
 * Display modes (setting "Display mode", phase 2b):
 *   DIRECT      original path: the receive thread decodes, draws and swaps.
 *   LOW_LATENCY the receive thread only decodes; a display thread draws the
 *               newest decoded frame as soon as it is ready (older ones are
 *               skipped). The network is no longer blocked by drawing.
 *   SMOOTH      same split, but the display thread shows exactly one frame per
 *               screen refresh, oldest first, so bursts of frames are spread
 *               out instead of overwriting each other. Costs about one frame of
 *               latency; a backlog that persists is trimmed automatically.
 * Decoded frames go into a small pool of textures ("slots") so that the decoder
 * never writes into the texture being drawn.
 */
#define VIDEO_SLOTS 3
#define SMOOTH_BACKLOG_LIMIT 30   /* refreshes with 2+ frames waiting before one is dropped */

enum { SLOT_FREE, SLOT_DECODING, SLOT_READY, SLOT_DRAWING };

typedef struct {
  vita2d_texture *tex;
  int state;
  int frame_number;
  uint64_t receive_us;   /* first packet of that frame, for latency statistics */
  uint32_t seq;          /* order in which frames became ready */
} video_slot;

static video_slot slots[VIDEO_SLOTS];
static int slot_count = 0;
static int display_mode = DISPLAY_MODE_DIRECT;
static uint32_t ready_seq = 0;
static SceUID slot_mutex = -1;   /* protects slot states */
static SceUID ready_sema = -1;   /* LOW_LATENCY: signalled when a frame is ready */
static SceUID draw_mutex = -1;   /* held while the display thread draws */
static SceUID display_thread = -1;
static volatile bool display_running = false;

SceAvcdecCtrl *decoder = NULL;
SceUID displayblock = -1;
SceUID decoderblock = -1;
SceUID pacer_thread = -1;
SceVideodecQueryInitInfoHwAvcdec *init = NULL;
SceAvcdecQueryDecoderInfo *decoder_info = NULL;

typedef struct {
  bool activated;
  uint8_t alpha;
  bool plus;
} indicator_status;

//static unsigned numframes;
static bool active_video_thread = true;
static bool active_pacer_thread = false;
static indicator_status poor_net_indicator = {0};

uint32_t frame_count = 0;
uint32_t need_drop = 0;
uint32_t curr_fps[2] = {0, 0};

typedef struct {
  unsigned int texture_width;
  unsigned int texture_height;
  float origin_x;
  float origin_y;
  float region_x1;
  float region_y1;
  float region_x2;
  float region_y2;
} image_scaling_settings;

static image_scaling_settings image_scaling = {0};

void update_scaling_settings(int width, int height) {
  image_scaling.texture_width = SCREEN_WIDTH;
  image_scaling.texture_height = SCREEN_HEIGHT;
  image_scaling.origin_x = 0;
  image_scaling.origin_y = 0;
  image_scaling.region_x1 = 0;
  image_scaling.region_y1 = 0;
  image_scaling.region_x2 = image_scaling.texture_width;
  image_scaling.region_y2 = image_scaling.texture_height;

  double scaled_width = (double) SCREEN_HEIGHT * width / height;
  double scaled_height = (double) SCREEN_WIDTH * height / width;

  if (SCREEN_WIDTH * height == SCREEN_HEIGHT * width) {
    // streaming resolution ratio matches Vita's screen ratio
    // use default setting
  } else if (SCREEN_WIDTH * height > SCREEN_HEIGHT * width) {
    // host ratio example: 4:3, 16:10
    // Vita ratio range: 2:16 (64 x 544) - native (960 x 544)
    if (config.center_region_only) {
      image_scaling.texture_height = VITA_DECODER_RESOLUTION(scaled_height);
      image_scaling.region_y1 = VITA_DECODER_RESOLUTION((scaled_height - SCREEN_HEIGHT) / 2);
      image_scaling.region_y2 = VITA_DECODER_RESOLUTION((scaled_height + SCREEN_HEIGHT) / 2);
    } else {
      image_scaling.texture_width = VITA_DECODER_RESOLUTION(scaled_width);
      image_scaling.region_x2 = VITA_DECODER_RESOLUTION(scaled_width);
      image_scaling.origin_x = round((double) (SCREEN_WIDTH - image_scaling.texture_width) / 2);
    }
  } else {
    // host ratio example: 16:9, 21:9, 32:9
    // Vita ratio range: native (960 x 544) - 15:1 (960 x 64)
    if (config.center_region_only) {
      image_scaling.texture_width = VITA_DECODER_RESOLUTION(scaled_width);
      image_scaling.region_x1 = VITA_DECODER_RESOLUTION((scaled_width - SCREEN_WIDTH) / 2);
      image_scaling.region_x2 = VITA_DECODER_RESOLUTION((scaled_width + SCREEN_WIDTH) / 2);
    } else {
      image_scaling.texture_height = VITA_DECODER_RESOLUTION(scaled_height);
      image_scaling.region_y2 = VITA_DECODER_RESOLUTION(scaled_height);
      image_scaling.origin_y = round((double) (SCREEN_HEIGHT - image_scaling.texture_height) / 2);
    }
  }

  printf("update_scaling_settings: width = %u\n", width);
  printf("update_scaling_settings: height = %u\n", height);
  printf("update_scaling_settings: scaled_width = %f\n", scaled_width);
  printf("update_scaling_settings: scaled_height = %f\n", scaled_height);
  printf("update_scaling_settings: image_scaling.texture_width = %u\n", image_scaling.texture_width);
  printf("update_scaling_settings: image_scaling.texture_height = %u\n", image_scaling.texture_height);
  printf("update_scaling_settings: image_scaling.origin_x = %f\n", image_scaling.origin_x);
  printf("update_scaling_settings: image_scaling.origin_y = %f\n", image_scaling.origin_y);
  printf("update_scaling_settings: image_scaling.region_x1 = %f\n", image_scaling.region_x1);
  printf("update_scaling_settings: image_scaling.region_y1 = %f\n", image_scaling.region_y1);
  printf("update_scaling_settings: image_scaling.region_x2 = %f\n", image_scaling.region_x2);
  printf("update_scaling_settings: image_scaling.region_y2 = %f\n", image_scaling.region_y2);
}

/*
 * Original frame pacer (enabled by "frame pacer" in the settings).
 * Once per second it compares the number of frames shown with the target fps
 * and schedules the excess to be dropped during the next second. It does not
 * follow the screen refresh; phase 2 replaces it with vblank-aligned display.
 */
static int vita_pacer_thread_main(SceSize args, void *argp) {
  const int wait = 1000000; // 1 s
  int max_fps = config.stream.fps;
  uint64_t last_check_time = sceKernelGetSystemTimeWide();
  need_drop = 0;
  frame_count = 0;
  while (active_pacer_thread) {
    uint32_t curr_frame_count = frame_count;
    frame_count = 0;

    // More frames shown than the stream rate during the last second: drop the
    // excess over the next second (only used in DIRECT display mode).
    if (active_video_thread && config.enable_frame_pacer && curr_frame_count > max_fps)
      need_drop += curr_frame_count - max_fps;

    curr_fps[0] = curr_frame_count;
    curr_fps[1] = max_fps;

    // Sleep so that iterations stay one second apart on average.
    uint64_t curr_check_time = sceKernelGetSystemTimeWide();
    uint32_t lapse = curr_check_time - last_check_time;
    last_check_time = curr_check_time;
    if (lapse > wait && (lapse - wait) < wait) {
      sceKernelDelayThread(wait * 2 - lapse);
    } else {
      sceKernelDelayThread(wait);
    }
  }
  return 0;
}

static void display_stop(void);
static bool display_start(void);

static void vita_cleanup() {
  display_stop();
  stream_stats_session_end();

  if (video_status == INIT_FRAME_PACER_THREAD) {
    active_pacer_thread = false;
    // wait 10sec
    SceUInt timeout = 10000000;
    int ret;
    sceKernelWaitThreadEnd(pacer_thread, &ret, &timeout);
    sceKernelDeleteThread(pacer_thread);
    video_status--;
  }

  if (video_status == INIT_AVC_DEC) {
    sceAvcdecDeleteDecoder(decoder);
    video_status--;
  }

  if (video_status == INIT_DECODER_MEMBLOCK) {
    if (decoderblock >= 0) {
      sceKernelFreeMemBlock(decoderblock);
      decoderblock = -1;
    }
    if (decoder != NULL) {
      free(decoder);
      decoder = NULL;
    }
    if (decoder_info != NULL) {
      free(decoder_info);
      decoder_info = NULL;
    }
    video_status--;
  }

  if (video_status == INIT_AVC_LIB) {
    sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);

    if (init != NULL) {
      free(init);
      init = NULL;
    }
    video_status--;
  }

  if (video_status == INIT_FRAMEBUFFER) {
    for (int i = 0; i < slot_count; i++) {
      if (slots[i].tex != NULL)
        vita2d_free_texture(slots[i].tex);
      slots[i].tex = NULL;
    }
    slot_count = 0;
    frame_texture = NULL;

    if (decoder_buffer != NULL) {
      free(decoder_buffer);
      decoder_buffer = NULL;
    }
    video_status--;
  }

  if (video_status == INIT_GS) {
    gs_sps_stop();
    video_status--;
  }
}

static int vita_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags) {
  int ret;
  printf("vita video setup\n");

  if (video_status == NOT_INIT) {
    // INIT_GS
    gs_sps_init(width, height);
    video_status++;
  }

  if (video_status == INIT_GS) {
    // INIT_FRAMEBUFFER
    update_scaling_settings(width, height);

    decoder_buffer_size = DECODER_BUFFER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE;
    decoder_buffer = malloc(decoder_buffer_size);
    if (decoder_buffer == NULL) {
      printf("decoder_buffer: not enough memory\n");
      ret = VITA_VIDEO_ERROR_NO_MEM;
      goto cleanup;
    } 

    display_mode = config.display_mode;
    if (display_mode < DISPLAY_MODE_DIRECT || display_mode > DISPLAY_MODE_SMOOTH)
      display_mode = DISPLAY_MODE_DIRECT;
    int wanted = display_mode == DISPLAY_MODE_DIRECT ? 1 : VIDEO_SLOTS;
    for (slot_count = 0; slot_count < wanted; slot_count++) {
      memset(&slots[slot_count], 0, sizeof(video_slot));
      slots[slot_count].tex = vita2d_create_empty_texture_format(image_scaling.texture_width, image_scaling.texture_height, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
      if (slots[slot_count].tex == NULL) {
        printf("frame_texture: not enough memory\n");
        ret = VITA_VIDEO_ERROR_NO_MEM;
        goto cleanup;
      }
    }
    frame_texture = slots[0].tex;

    printf("Initalized Framebuffer");

    video_status++;
  }

  if (video_status == INIT_FRAMEBUFFER) {
    // INIT_AVC_LIB
    if (init == NULL) {
      init = calloc(1, sizeof(SceVideodecQueryInitInfoHwAvcdec));
      if (init == NULL) {
        printf("init_framebuffer: not enough memory\n");
        ret = VITA_VIDEO_ERROR_NO_MEM;
        goto cleanup;
      }
    }
    init->size = sizeof(SceVideodecQueryInitInfoHwAvcdec);
    init->horizontal = VITA_DECODER_RESOLUTION(width);
    init->vertical = VITA_DECODER_RESOLUTION(height);
    init->numOfRefFrames = 4;
    init->numOfStreams = 1;

    ret = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, init);
    if (ret < 0) {
      printf("sceVideodecInitLibrary 0x%x\n", ret);
      ret = VITA_VIDEO_ERROR_INIT_LIB;
      goto cleanup;
    }
    printf("Initalized AVC library");
    video_status++;
  }

  if (video_status == INIT_AVC_LIB) {
    // INIT_DECODER_MEMBLOCK
    if (decoder_info == NULL) {
      decoder_info = calloc(1, sizeof(SceAvcdecQueryDecoderInfo));
      if (decoder_info == NULL) {
        printf("decoder_info: not enough memory\n");
        ret = VITA_VIDEO_ERROR_NO_MEM;
        goto cleanup;
      }
    }
    decoder_info->horizontal = init->horizontal;
    decoder_info->vertical = init->vertical;
    decoder_info->numOfRefFrames = init->numOfRefFrames;

    SceAvcdecDecoderInfo decoder_info_out = {0};

    ret = sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder_info, &decoder_info_out);
    if (ret < 0) {
      printf("sceAvcdecQueryDecoderMemSize 0x%x size 0x%x\n", ret, decoder_info_out.frameMemSize);
      ret = VITA_VIDEO_ERROR_QUERY_DEC_MEMSIZE;
      goto cleanup;
    }

    decoder = calloc(1, sizeof(SceAvcdecCtrl));
    if (decoder == NULL) {
      printf("not enough memory\n");
      ret = VITA_VIDEO_ERROR_ALLOC_MEM;
      goto cleanup;
    }

    size_t sz = (decoder_info_out.frameMemSize + 0xFFFFF) & ~0xFFFFF;
    decoder->frameBuf.size = sz;
    printf("allocating size 0x%x\n", sz);

    decoderblock = sceKernelAllocMemBlock("decoder", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, sz, NULL);
    if (decoderblock < 0) {
      printf("decoderblock: 0x%08x\n", decoderblock);
      ret = VITA_VIDEO_ERROR_ALLOC_MEM;
      goto cleanup;
    }

    ret = sceKernelGetMemBlockBase(decoderblock, &decoder->frameBuf.pBuf);
    if (ret < 0) {
      printf("sceKernelGetMemBlockBase: 0x%x\n", ret);
      ret = VITA_VIDEO_ERROR_GET_MEMBASE;
      goto cleanup;
    }
    video_status++;
  }

  if (video_status == INIT_DECODER_MEMBLOCK) {
    // INIT_AVC_DEC
    printf("base: 0x%08x\n", (unsigned int)decoder->frameBuf.pBuf);

    ret = sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder, decoder_info);
    if (ret < 0) {
      printf("sceAvcdecCreateDecoder 0x%x\n", ret);
      ret = VITA_VIDEO_ERROR_CREATE_DEC;
      goto cleanup;
    }
    video_status++;
  }

  if (video_status == INIT_AVC_DEC) {
    // INIT_FRAME_PACER_THREAD
    ret = sceKernelCreateThread("frame_pacer", vita_pacer_thread_main, 0, 0x10000, 0, 0, NULL);
    if (ret < 0) {
      printf("sceKernelCreateThread 0x%x\n", ret);
      ret = VITA_VIDEO_ERROR_CREATE_PACER_THREAD;
      goto cleanup;
    }
    pacer_thread = ret;
    active_pacer_thread = true;
    sceKernelStartThread(pacer_thread, 0, NULL);
    video_status++;
  }

  if (display_mode != DISPLAY_MODE_DIRECT && !display_start()) {
    printf("display thread failed, falling back to direct mode\n");
    display_mode = DISPLAY_MODE_DIRECT;
  }

  stream_stats_session_start(width, height, redrawRate);

  return VITA_VIDEO_INIT_OK;

cleanup:
  vita_cleanup();
  return ret;
}

/* ---------------- slots and display thread (LOW_LATENCY / SMOOTH) ---------------- */

/* Receive thread: get a texture to decode into. If none is free, the oldest
 * frame still waiting is dropped (it would have been shown too late). */
static int slot_acquire_for_decode(void) {
  int pick = -1;
  sceKernelLockMutex(slot_mutex, 1, NULL);
  for (int i = 0; i < slot_count; i++) {
    if (slots[i].state == SLOT_FREE) { pick = i; break; }
  }
  if (pick < 0) {
    for (int i = 0; i < slot_count; i++) {
      if (slots[i].state == SLOT_READY && (pick < 0 || slots[i].seq < slots[pick].seq))
        pick = i;
    }
    if (pick >= 0)
      stream_stats_on_frame_replaced();
  }
  if (pick < 0)
    pick = 0; /* cannot happen with one decoder and one drawer; stay safe */
  slots[pick].state = SLOT_DECODING;
  sceKernelUnlockMutex(slot_mutex, 1);
  return pick;
}

static void slot_set_free(int slot) {
  sceKernelLockMutex(slot_mutex, 1, NULL);
  slots[slot].state = SLOT_FREE;
  sceKernelUnlockMutex(slot_mutex, 1);
}

/* Receive thread: a decoded picture is ready to be shown. */
static void slot_publish(int slot, int frame_number, uint64_t receive_us) {
  sceKernelLockMutex(slot_mutex, 1, NULL);
  slots[slot].frame_number = frame_number;
  slots[slot].receive_us = receive_us;
  slots[slot].seq = ++ready_seq;
  slots[slot].state = SLOT_READY;
  sceKernelUnlockMutex(slot_mutex, 1);
  if (display_mode == DISPLAY_MODE_LOW_LATENCY)
    sceKernelSignalSema(ready_sema, 1);
}

/* Display thread: choose the frame to draw. LOW_LATENCY takes the newest and
 * frees the others; SMOOTH takes the oldest and trims a lasting backlog. */
static int slot_pick_for_display(int *backlog) {
  int pick = -1, ready = 0;
  sceKernelLockMutex(slot_mutex, 1, NULL);
  for (int i = 0; i < slot_count; i++) {
    if (slots[i].state != SLOT_READY)
      continue;
    ready++;
    if (pick < 0)
      pick = i;
    else if (display_mode == DISPLAY_MODE_LOW_LATENCY ? slots[i].seq > slots[pick].seq
                                                      : slots[i].seq < slots[pick].seq)
      pick = i;
  }
  if (pick >= 0) {
    slots[pick].state = SLOT_DRAWING;
    if (display_mode == DISPLAY_MODE_LOW_LATENCY) {
      for (int i = 0; i < slot_count; i++) {
        if (slots[i].state == SLOT_READY) {
          slots[i].state = SLOT_FREE;
          stream_stats_on_frame_replaced();
        }
      }
    } else {
      // SMOOTH: 2+ frames waiting at every refresh means latency has grown by a
      // frame for good (e.g. host slightly faster than the screen): drop one.
      *backlog = ready >= 2 ? *backlog + 1 : 0;
      if (*backlog > SMOOTH_BACKLOG_LIMIT) {
        int oldest = -1;
        for (int i = 0; i < slot_count; i++) {
          if (slots[i].state == SLOT_READY && (oldest < 0 || slots[i].seq < slots[oldest].seq))
            oldest = i;
        }
        if (oldest >= 0) {
          slots[oldest].state = SLOT_FREE;
          stream_stats_on_frame_replaced();
        }
        *backlog = 0;
      }
    }
  }
  sceKernelUnlockMutex(slot_mutex, 1);
  stream_stats_on_display_queue((uint32_t)ready);
  return pick;
}

static int display_thread_main(SceSize args, void *argp) {
  int backlog = 0;
  while (display_running) {
    // Wait for something to show: a new frame (LOW_LATENCY) or the next screen refresh (SMOOTH).
    if (display_mode == DISPLAY_MODE_SMOOTH) {
      sceDisplayWaitVblankStart();
    } else {
      SceUInt timeout = 100000;
      if (sceKernelWaitSema(ready_sema, 1, &timeout) < 0)
        continue;
    }
    if (!display_running)
      break;

    int slot = slot_pick_for_display(&backlog);
    if (slot < 0) {
      if (display_mode == DISPLAY_MODE_SMOOTH)
        stream_stats_on_display_idle(); // a refresh with no new frame
      continue;
    }
    if (!active_video_thread) {         // stream paused: menus own the screen
      slot_set_free(slot);
      continue;
    }

    sceKernelLockMutex(draw_mutex, 1, NULL);
    uint64_t t_render0 = LiGetMicroseconds();
    vita2d_start_drawing();
    draw_streaming(slots[slot].tex);
    draw_fps();
    draw_indicators();
    vita2d_end_drawing();
    vita2d_wait_rendering_done();       // the texture has been copied: it can be reused
    uint64_t t_render1 = LiGetMicroseconds();
    uint64_t receive_us = slots[slot].receive_us;
    slot_set_free(slot);
    vita2d_swap_buffers();              // flip at the next refresh (no tearing)
    uint64_t t_swap1 = LiGetMicroseconds();
    sceKernelUnlockMutex(draw_mutex, 1);

    frame_count++;
    stream_stats_on_present((uint32_t)(t_render1 - t_render0), (uint32_t)(t_swap1 - t_render1),
                            receive_us, t_swap1);
  }
  return 0;
}

static bool display_start(void) {
  slot_mutex = sceKernelCreateMutex("vid_slots", 0, 0, NULL);
  draw_mutex = sceKernelCreateMutex("vid_draw", 0, 0, NULL);
  ready_sema = sceKernelCreateSema("vid_ready", 0, 0, 64, NULL);
  if (slot_mutex < 0 || draw_mutex < 0 || ready_sema < 0)
    return false;
  ready_seq = 0;
  display_running = true;
  display_thread = sceKernelCreateThread("vid_display", display_thread_main, 64, 0x10000, 0, 0, NULL);
  if (display_thread < 0) {
    display_running = false;
    return false;
  }
  sceKernelStartThread(display_thread, 0, NULL);
  return true;
}

static void display_stop(void) {
  if (display_running) {
    display_running = false;
    if (ready_sema >= 0)
      sceKernelSignalSema(ready_sema, 1);
    SceUInt timeout = 1000000;
    int thread_ret = 0;
    sceKernelWaitThreadEnd(display_thread, &thread_ret, &timeout);
    sceKernelDeleteThread(display_thread);
  }
  display_thread = -1;
  if (ready_sema >= 0) { sceKernelDeleteSema(ready_sema); ready_sema = -1; }
  if (draw_mutex >= 0) { sceKernelDeleteMutex(draw_mutex); draw_mutex = -1; }
  if (slot_mutex >= 0) { sceKernelDeleteMutex(slot_mutex); slot_mutex = -1; }
}

/*
 * Called by moonlight-common-c for each complete video frame.
 *
 * With CAPABILITY_DIRECT_SUBMIT this runs on the video receive thread itself:
 * until it returns, newly arrived packets wait in the socket buffer. Steps:
 *   1. statistics: arrival times of the frame
 *   2. copy the frame into one buffer for the decoder (SPS header patched on the way)
 *   3. hardware H.264 decode, written straight into the texture shown on screen
 *   4. draw that texture and swap the screen buffers
 * Decode and display are therefore in series with the network; phase 2 splits them.
 */
static int vita_submit_decode_unit(PDECODE_UNIT decodeUnit) {
  SceAvcdecAu au = {0};
  SceAvcdecArrayPicture array_picture = {0};
  struct SceAvcdecPicture picture = {0};
  struct SceAvcdecPicture *pictures = { &picture };
  array_picture.numOfElm = 1;
  array_picture.pPicture = &pictures;

  // The network receive thread runs this callback: give it the highest user
  // priority once, so packets are read promptly (VitaRPS5 uses 64 as well).
  static SceUID boosted_thread = -1;
  if (boosted_thread != sceKernelGetThreadId()) {
    boosted_thread = sceKernelGetThreadId();
    sceKernelChangeThreadPriority(boosted_thread, 64);
  }

  // 1. Statistics: frame number, size and arrival timestamps (see stream_stats.c).
  stream_stats_on_frame(decodeUnit->frameNumber, decodeUnit->frameType, decodeUnit->fullLength,
                        decodeUnit->receiveTimeUs, decodeUnit->enqueueTimeUs,
                        decodeUnit->frameHostProcessingLatency, LiGetMicroseconds());

  picture.size = sizeof(picture);
  picture.frame.pixelType = 0;
  picture.frame.framePitch = image_scaling.texture_width;
  picture.frame.frameWidth = image_scaling.texture_width;
  picture.frame.frameHeight = image_scaling.texture_height;
  // In LOW_LATENCY / SMOOTH mode, decode into a texture nobody is drawing.
  int slot = display_mode == DISPLAY_MODE_DIRECT ? 0 : slot_acquire_for_decode();
  picture.frame.pPicture[0] = vita2d_texture_get_datap(slots[slot].tex);

  //ensure_buf_size((void *)&decoder_buffer, &decoder_buffer_size, decodeUnit->fullLength + 64);

  if (decoder_buffer_size < (decodeUnit->fullLength + AV_INPUT_BUFFER_PADDING_SIZE)) {
    printf("Reallocating decoder buffer to %u bytes", (size_t)decodeUnit->fullLength + AV_INPUT_BUFFER_PADDING_SIZE);
    decoder_buffer = realloc(decoder_buffer, decodeUnit->fullLength + AV_INPUT_BUFFER_PADDING_SIZE);
    decoder_buffer_size = decodeUnit->fullLength+AV_INPUT_BUFFER_PADDING_SIZE;
    if (decoder_buffer == NULL) {
      printf("Out of memory! could not reallocate buffer!!");
      exit(1);
    }
  }


/*   if (decodeUnit->fullLength >= DECODER_BUFFER_SIZE + 64) {
    printf("Video decode buffer too small\n");
    exit(1);
  } */

  // 2. The frame arrives as a chain of NAL buffers; concatenate them. The SPS
  //    (stream parameters) goes through gs_sps_fix (libgamestream/sps.c), which sets the
  //    low-latency hints for the decoder: no reordering, max_dec_frame_buffering = 1.
  PLENTRY entry = decodeUnit->bufferList;
  uint32_t length = 0;
  while (entry != NULL) {
    if (entry->bufferType == BUFFER_TYPE_SPS) {
      gs_sps_fix(entry, GS_SPS_BITSTREAM_FIXUP, decoder_buffer, &length);
    } else {
      memcpy(decoder_buffer+length, entry->data, entry->length);
      length += entry->length;
    }
    entry = entry->next;
  }

  au.es.pBuf = decoder_buffer;
  au.es.size = decodeUnit->fullLength;
  au.dts.lower = 0xFFFFFFFF;
  au.dts.upper = 0xFFFFFFFF;
  // Tag the access unit with its frame number: the decoder copies the pts of the
  // picture it outputs into picture.info.pts, which tells which frame is shown and
  // how many frames the decoder is holding back.
  au.pts.lower = (uint32_t)decodeUnit->frameNumber;
  au.pts.upper = 0;
  picture.info.pts.lower = 0xFFFFFFFF;
  picture.info.pts.upper = 0xFFFFFFFF;

  // 3. Decode. The output picture is written directly into frame_texture.
  uint64_t t_dec0 = LiGetMicroseconds();
  int ret = 0;
  ret = sceAvcdecDecode(decoder, &au, &array_picture);
  uint32_t dec_us = (uint32_t)(LiGetMicroseconds() - t_dec0);
  if (ret < 0) {
    if (display_mode != DISPLAY_MODE_DIRECT)
      slot_set_free(slot);
    stream_stats_on_decode(dec_us, ret, false, STREAM_STATS_HOLD_UNKNOWN);
    printf("sceAvcdecDecode (len=0x%x): 0x%x numOfOutput %d\n", decodeUnit->fullLength, ret, array_picture.numOfOutput);
    return DR_NEED_IDR;
  }

  // The decoder may keep frames back. The pts read back tells which frame came
  // out: hold = frame just fed - frame that came out (0 = nothing held back).
  bool has_output = array_picture.numOfOutput == 1;
  int shown_frame = decodeUnit->frameNumber;
  uint32_t hold = STREAM_STATS_HOLD_UNKNOWN;
  if (has_output && picture.info.pts.upper == 0) {
    uint32_t lag = (uint32_t)decodeUnit->frameNumber - picture.info.pts.lower;
    if (lag < 64) {
      hold = lag;
      shown_frame = (int)picture.info.pts.lower;
    }
  }
  stream_stats_on_decode(dec_us, ret, has_output, hold);

  if (!has_output) {
    //printf("numOfOutput %d\n", array_picture.numOfOutput);
    if (display_mode != DISPLAY_MODE_DIRECT)
      slot_set_free(slot);
    return DR_OK;
  }

  // LOW_LATENCY / SMOOTH: hand the picture to the display thread and return to
  // the network immediately.
  if (display_mode != DISPLAY_MODE_DIRECT) {
    slot_publish(slot, shown_frame, stream_stats_receive_time(shown_frame));
    return DR_OK;
  }

  //TODO: Seems silly to decode the unit if we're going to drop the frame?
  // Find out why we decode or if we even need to
  // 4. Display, unless the frame pacer asked to drop this frame.
  if (active_video_thread) {
    if (need_drop > 0) {
      vita_debug_log("remain frameskip: %d\n", need_drop);
      // skip
      need_drop--;
      stream_stats_on_pacer_drop();
    } else {
      uint64_t t_render0 = LiGetMicroseconds();
      vita2d_start_drawing();

      draw_streaming(frame_texture);
      draw_fps();
      draw_indicators();

      vita2d_end_drawing();

      vita2d_wait_rendering_done();   // GPU finished drawing
      uint64_t t_render1 = LiGetMicroseconds();
      vita2d_swap_buffers();          // queue the new image for the screen (vblank wait if enabled)
      uint64_t t_swap1 = LiGetMicroseconds();

      frame_count++;
      stream_stats_on_present((uint32_t)(t_render1 - t_render0), (uint32_t)(t_swap1 - t_render1),
                              stream_stats_receive_time(shown_frame), t_swap1);
    }
  }

  // if (numframes++ % 6 == 0)
  //   return DR_NEED_IDR;

  return DR_OK;
}

void draw_streaming(vita2d_texture *frame_texture) {
  // ui is still rendering in the background, clear the screen first
  vita2d_clear_screen();
  vita2d_draw_texture_part(frame_texture,
                           image_scaling.origin_x,
                           image_scaling.origin_y,
                           image_scaling.region_x1,
                           image_scaling.region_y1,
                           image_scaling.region_x2,
                           image_scaling.region_y2);
}

void draw_fps() {
  // "Show FPS" now shows the full per-second statistics overlay.
  if (config.show_fps) {
    stream_stats_draw_overlay();
  }
}

void draw_indicators() {
  if (poor_net_indicator.activated) {
    vita2d_font_draw_text(font, 40, 500, RGBA8(0xFF, 0xFF, 0xFF, poor_net_indicator.alpha), 64, ICON_NETWORK);
    poor_net_indicator.alpha += (0x4 * (poor_net_indicator.plus ? 1 : -1));
    if (poor_net_indicator.alpha == 0) {
      poor_net_indicator.plus = !poor_net_indicator.plus;
      poor_net_indicator.alpha += (0x4 * (poor_net_indicator.plus ? 1 : -1));
    }
  }

  if (dc_tracker.currently_sprinting) {
    vita2d_font_draw_text(font, 40, 50, RGBA8(0xFF, 0xFF, 0xFF, 0xAA), 48, ICON_SPRINTING);
  }

}

void vitavideo_start() {
  active_video_thread = true;
  // The display thread paces itself; vita2d must not block its swaps.
  vita2d_set_vblank_wait(display_mode == DISPLAY_MODE_DIRECT ? config.enable_vita_vblank_wait : false);
}

void vitavideo_stop() {
  vita2d_set_vblank_wait(true);
  active_video_thread = false;
  // Let a draw in progress finish before the menus start drawing.
  if (draw_mutex >= 0) {
    sceKernelLockMutex(draw_mutex, 1, NULL);
    sceKernelUnlockMutex(draw_mutex, 1);
  }
}

void vitavideo_show_poor_net_indicator() {
  poor_net_indicator.activated = true;
}

void vitavideo_hide_poor_net_indicator() {
  //poor_net_indicator.activated = false;
  memset(&poor_net_indicator, 0, sizeof(indicator_status));
}

int vitavideo_initialized() {
  return video_status != NOT_INIT;
}

DECODER_RENDERER_CALLBACKS decoder_callbacks_vita = {
  .setup = vita_setup,
  .cleanup = vita_cleanup,
  .submitDecodeUnit = vita_submit_decode_unit,
  .capabilities = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SLICES_PER_FRAME(2)
};
