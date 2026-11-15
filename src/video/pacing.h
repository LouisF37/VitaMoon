/*
 * Paced display: when to show each frame.
 *
 * Frames are shown at the cadence the PC captured them (its presentation
 * times), which a Wi-Fi link does not keep: a frame is due at an anchor on the
 * Vita clock plus the capture interval since the anchor frame. The display
 * picks a frame some time before each flip (its drawing budget, the "guard"),
 * so a frame must be ready that long before it is due.
 *
 * Every 2 s the anchor is checked against the slowest frame of the window:
 * moved later, just enough, when that frame was late (Wi-Fi jitter, or the
 * PC's clock running slower than the Vita's); moved earlier when even that
 * frame waited more than a margin plus 2 ms (a faster PC clock, a calmer
 * link); left alone in between, so that due times do not slide back and forth
 * across a screen refresh. At most 4 ms per window. The margin, the extra wait
 * tolerated, starts at 12 ms (dubleyu/vita-moonlight-relay's fixed buffer),
 * grows by 4 ms after a window with two or more late frames and shrinks by
 * 2 ms after five windows without one, down to 4 ms.
 *
 * Pure logic, no system calls: the video code calls it under its slot lock,
 * and tools/tests runs it on a PC against simulated streams.
 */
#ifndef VIDEO_PACING_H
#define VIDEO_PACING_H

#include <stdbool.h>
#include <stdint.h>

#define PACE_DELAY_START_US  12000   /* margin to begin with */
#define PACE_DELAY_MIN_US     4000
#define PACE_DELAY_MAX_US    33000
#define PACE_DELAY_UP_US      4000   /* after a window with 2+ late frames */
#define PACE_DELAY_DOWN_US    2000   /* after 5 windows without one */
#define PACE_WINDOW_US     2000000
#define PACE_REALIGN_MAX_US   4000   /* largest anchor move per window, either way */
#define PACE_HOLD_US          2000   /* extra wait tolerated before moving the anchor earlier */
#define PACE_JUMP_FUTURE_US 250000   /* re-anchor when a frame is due this far ahead... */
#define PACE_JUMP_PAST_US   500000   /* ...or was due this long ago */

typedef struct {
  bool valid;
  uint64_t anchor_pts;      /* PC time of the anchor frame */
  uint64_t anchor_local;    /* Vita time it is due (delay included) */
  /* Margin kept by the slowest frame of a window before the point it must be
   * ready by. Not the whole wait: faster frames wait longer. */
  uint32_t delay_us;
  uint64_t window_start;
  int64_t window_min_slack;
  uint32_t window_late;
  uint32_t calm_windows;
} pace_state;

/* A decoded frame waiting to be shown. */
typedef struct {
  bool ready;
  uint64_t pts_us;
  uint32_t seq;             /* order in which frames became ready */
} pace_frame;

void pace_reset(pace_state *p);

/* When a frame captured at pts_us is due, on the Vita clock. Anchors on this
 * frame if there is no anchor yet or the PC's clock went back. */
uint64_t pace_target(pace_state *p, uint64_t pts_us, uint64_t now_us);

/* A frame captured at pts_us became ready at now_us. guard_us is how long
 * before its flip the display picks a frame (its drawing budget): a frame ready
 * later than that misses its flip, so it counts as late. Every PACE_WINDOW_US,
 * adjusts the delay and the anchor; returns true when it did. */
bool pace_observe(pace_state *p, uint64_t pts_us, uint64_t now_us, uint32_t guard_us);

/* Chooses the frame to show for the flip visible at `deadline`: the newest
 * ready frame that is due (catch_up: the newest ready frame, now, and the clock
 * is anchored on it). Older ready frames are set in *drop (bit i = frames[i]).
 * Returns the index, or -1 when no frame is due. n <= 32. */
int pace_pick(pace_state *p, const pace_frame *frames, int n, uint64_t deadline, bool catch_up,
              uint32_t *drop);

#endif
