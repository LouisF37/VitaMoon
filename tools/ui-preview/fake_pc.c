/*
 * UI preview: a PC running Sunshine, as libgamestream and moonlight-common-c
 * see it. The menus and src/connection.c run for real against it; what they
 * ask of the PC is written to a log that the journeys check (journeys.c).
 *
 * It answers as Sunshine 2026.914 does where the menus depend on it: a resume
 * with no game running and a launch while another game runs are refused, and
 * a stream start goes through the stages of LiStartConnection, calling the
 * listener callbacks on the calling thread as moonlight-common-c does.
 */
#include "preview.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <Limelight.h>
#include "client.h"
#include "errors.h"
#include "platform.h"

extern const char *gs_error;
extern int gs_status_code;

preview_pc_t preview_pc = {
  .answers = true,
  .paired = true,
  .running = 0,
  .pair_result = GS_OK,
};

/* ---------------- log of what the Vita asked ---------------- */

#define LOG_MAX 256
static char log_lines[LOG_MAX][96];
static int log_count = 0;

void preview_record(const char *fmt, ...) {
  if (log_count >= LOG_MAX)
    return;
  va_list va;
  va_start(va, fmt);
  vsnprintf(log_lines[log_count++], sizeof(log_lines[0]), fmt, va);
  va_end(va);
}

int preview_log_count(void) {
  return log_count;
}

const char *preview_log_line(int i) {
  return i >= 0 && i < log_count ? log_lines[i] : "";
}

/* ---------------- libgamestream ---------------- */

/* client.c and xml.c are the real ones, over fake_http.c. Pairing is the
 * exception: its exchange of certificates is not reproduced, gs_pair() below
 * stands in for it (client.c is built with its own renamed). */
int gs_pair(PSERVER_DATA server, char *pin, bool (*cancel)(void)) {
  preview_record("pair");
  gs_status_code = preview_pc.pair_status;
  if (preview_pc.pair_result == GS_OK) {
    preview_pc.paired = true;
    server->paired = true;
  } else {
    gs_error = preview_pc.pair_result == GS_CANCELLED ? "Cancelled" : "Pairing refused (preview)";
  }
  return preview_pc.pair_result;
}

/* Identifiers in the requests: their value does not matter here. */
void uuid_generate_random(unsigned char *uu) { memset(uu, 0x42, 16); }
void uuid_unparse(const unsigned char *uu, char *out) { strcpy(out, "42424242-4242-4242-4242-424242424242"); }

/* ---------------- moonlight-common-c ---------------- */

static bool stream_open = false;

/* The stages run on the calling thread, each with its callbacks; the stream
 * then starts (connectionStarted), or a stage fails and nothing is left open. */
int LiStartConnection(PSERVER_INFORMATION server, PSTREAM_CONFIGURATION config, PCONNECTION_LISTENER_CALLBACKS cl,
                      PDECODER_RENDERER_CALLBACKS dr, PAUDIO_RENDERER_CALLBACKS ar, void *render, int flags,
                      void *audio_context, int audio_flags) {
  if (stream_open)
    preview_record("connect while a stream is open");
  /* Sunshine's RTSP server only knows a session prepared by launch or resume */
  if (!preview_pc.session_ready) {
    preview_record("connect refused: no launch or resume before");
    return -1;
  }
  preview_pc.session_ready = false;
  /* not a step the journeys check unless they expect it */
  preview_record("bitrate %d", config->bitrate);
  bool fail = preview_pc.fail_connect > 0;
  if (fail)
    preview_pc.fail_connect--;
  for (int stage = STAGE_PLATFORM_INIT; stage <= STAGE_INPUT_STREAM_START; stage++) {
    cl->stageStarting(stage);
    if (fail && stage == STAGE_RTSP_HANDSHAKE) {
      cl->stageFailed(stage, -1);
      preview_record("connect failed");
      return -1;
    }
    cl->stageComplete(stage);
  }
  stream_open = true;
  preview_record("connect");
  cl->connectionStarted();
  return 0;
}

/* As moonlight-common-c: undoes the stages started, nothing when none was. */
void LiStopConnection(void) {
  if (!stream_open)
    return;
  stream_open = false;
  preview_record("stop");
}

bool preview_stream_open(void) {
  return stream_open;
}

const char *LiGetStageName(int stage) { return "stage"; }
void LiInitializeServerInformation(PSERVER_INFORMATION info) { memset(info, 0, sizeof(*info)); }
const char *LiGetLaunchUrlQueryParameters(void) { return "&corever=1"; }

static DECODER_RENDERER_CALLBACKS fake_video;
PDECODER_RENDERER_CALLBACKS platform_get_video(enum platform system) { return &fake_video; }
PAUDIO_RENDERER_CALLBACKS platform_get_audio(enum platform system) { return NULL; }
