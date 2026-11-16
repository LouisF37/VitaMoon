#ifndef MOON_H
#define MOON_H

/* The waiting moon: which of its pictures to show (assets/moon_00.png, a new
 * moon, to moon_11.png, the full moon, which is the VitaMoon mark). Kept free
 * of Vita calls: tested on a PC (tools/tests/test_ui.c). The drawing is in
 * guilib.c, its uses in ui_connect.c. */

#include <stdbool.h>
#include <stdint.h>

#define MOON_PHASES 12
/* Side of one picture in assets/moon.png (the pictures are side by side). */
#define MOON_CELL 112

/* Least time between two pictures drawn during a connection: each one is a
 * full menu frame on the thread that runs the connection. */
#define MOON_MIN_INTERVAL_US 50000

/* Picture for `done` steps out of `total`, rounded to the nearest. */
static inline int moon_phase(unsigned int done, unsigned int total) {
  if (total == 0 || done >= total)
    return MOON_PHASES - 1;
  return (int)((done * (MOON_PHASES - 1) + total / 2) / total);
}

/* Whether to draw again now: always for the last picture, otherwise not
 * sooner than MOON_MIN_INTERVAL_US after the previous one (last_us 0: none yet). */
static inline bool moon_should_draw(uint64_t last_us, uint64_t now_us, int phase) {
  return last_us == 0 || phase == MOON_PHASES - 1 || now_us - last_us >= MOON_MIN_INTERVAL_US;
}

#endif // MOON_H
