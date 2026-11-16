#ifndef RECONNECT_H
#define RECONNECT_H

/* Automatic reconnection after a lost stream (Wi-Fi drop, Vita put to sleep):
 * the decisions, kept free of Vita calls so that they are tested on a PC
 * (tools/tests/test_misc.c). The screen and the retries are in
 * src/gui/ui_connect.c. */

#include <stdbool.h>
#include <Limelight.h>

/* How long the Wi-Fi and the PC are waited for before giving up, and the time
 * between two tries. */
#define RECONNECT_GIVE_UP_MS 60000
#define RECONNECT_RETRY_MS 3000

/* A stream lost again sooner than this after it started counts as a failed
 * reconnection; after RECONNECT_MAX_QUICK_LOSSES of them in a row, Lune stops
 * reconnecting (the link is not good enough to hold a stream). */
#define RECONNECT_QUICK_LOSS_MS 20000
#define RECONNECT_MAX_QUICK_LOSSES 3

/* True when a stream that ended with this moonlight-common-c error was lost on
 * the way (network, sleep), false when the PC or the user ended it, or when the
 * PC reported a problem that a new stream would meet again. */
static inline bool reconnect_after_error(int error_code) {
  switch (error_code) {
    case ML_ERROR_GRACEFUL_TERMINATION:          /* ended by the PC or by the user */
    case ML_ERROR_UNEXPECTED_EARLY_TERMINATION:  /* capture error on the PC */
    case ML_ERROR_PROTECTED_CONTENT:             /* DRM content on the PC */
    case ML_ERROR_FRAME_CONVERSION:              /* the PC cannot convert the picture */
      return false;
    default:                                     /* timeouts, socket errors */
      return true;
  }
}

/* Called when a stream has been lost after running stream_ms milliseconds:
 * counts the streams lost soon after they started (in *quick_losses) and
 * returns false once reconnecting should stop. */
static inline bool reconnect_should_retry(int *quick_losses, unsigned stream_ms) {
  if (stream_ms < RECONNECT_QUICK_LOSS_MS)
    (*quick_losses)++;
  else
    *quick_losses = 0;
  return *quick_losses < RECONNECT_MAX_QUICK_LOSSES;
}

#endif // RECONNECT_H
