/* UI preview: scenario script helpers (see preview_sys.c). */
#pragma once

void preview_set_output(const char *dir);
void preview_wait(int frames);
void preview_press(unsigned int buttons);
void preview_tap(int x, int y);
void preview_snap(const char *name);
/* Lets the preview run this many frames past the end of the script. */
void preview_idle_tail(int frames);

#include <stdbool.h>

/* Pairing keys of the fake PCs, made once by libgamestream and kept. */
#define PREVIEW_KEY_DIR "/tmp/vitamoon-preview-keys"

/* The PC the menus talk to (fake_pc.c). Journeys set it up before they start. */
typedef struct {
  bool answers;       /* gs_init() reaches it */
  bool paired;        /* it knows this Vita */
  int running;        /* app running on it, 0 for none */
  int refuse_start;   /* next launch or resume requests it refuses */
  int fail_connect;   /* next stream starts that fail at the RTSP stage */
  int pair_result;    /* what gs_pair() returns (GS_OK, GS_CANCELLED, GS_ERROR) */
  int pair_status;    /* gs_status_code with it (409, 408...) */
  bool session_ready; /* a launch or resume was accepted: a stream can start */
} preview_pc_t;
extern preview_pc_t preview_pc;

/* What the Vita asked of the PC, in order ("launch 1005", "connect", "stop"...). */
void preview_record(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int preview_log_count(void);
const char *preview_log_line(int i);
bool preview_stream_open(void);

/* Presses buttons from the next frame on (and releases them the frame after). */
void preview_inject(unsigned buttons);

/* Journeys (journeys.c): returns false when name is not one. */
bool journey_run(const char *name);
