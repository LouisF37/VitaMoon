#ifndef INPUT_TAP_H
#define INPUT_TAP_H

/* Front screen as a trackpad: is a touch a click or the start of a swipe?
 * Kept free of Vita calls: tested on a PC (tools/tests/test_library.c).
 *
 * As in Moonlight for Android (RelativeTouchContext: TAP_TIME_THRESHOLD 250 ms,
 * TAP_MOVEMENT_THRESHOLD 20): a touch lifted within the tap time, without
 * moving more than TAP_MOVE_PX, clicks. A touch that moves further starts
 * moving the pointer at once, without waiting for the tap time to pass; one
 * that stays still longer than the tap time moves it too. VitaMoon used to
 * count only the time (100 ms, too short for many taps) and never the
 * movement, so a swipe could not move the pointer before 100 ms. */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define TAP_MOVE_PX 20   /* in screen pixels (960 x 544) */

typedef enum {
  TAP_UNDECIDED,   /* still down, short and still: wait */
  TAP_CLICK,       /* lifted in time without moving: a click */
  TAP_SWIPE        /* moved, or held too long: the pointer follows */
} tap_verdict;

/* elapsed_us since the first finger touched; dx, dy from where it touched;
 * lifted: fewer fingers than at the start. */
static inline tap_verdict tap_judge(uint64_t elapsed_us, unsigned int tap_time_ms, int dx, int dy, bool lifted) {
  bool moved = abs(dx) > TAP_MOVE_PX || abs(dy) > TAP_MOVE_PX;
  if (moved)
    return TAP_SWIPE;
  if (elapsed_us >= (uint64_t)tap_time_ms * 1000u)
    return TAP_SWIPE;
  return lifted ? TAP_CLICK : TAP_UNDECIDED;
}

#endif // INPUT_TAP_H
