/*
 * Pure parts of the log (src/log.c): the byte ring the lines wait in, the
 * per-second budget and the folding of repeated lines. No Vita calls, no
 * locking (the caller holds the log lock): tested on a PC
 * (tools/tests/test_log.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Lines wait here until the writer thread puts them on the memory card.
 * 64 KiB holds several seconds of the busiest detailed log. */
#define LOG_RING_SIZE (64 * 1024)

typedef struct {
  char buf[LOG_RING_SIZE];
  size_t head;          /* next byte to read */
  size_t used;          /* bytes waiting */
  uint32_t dropped;     /* lines refused (ring full) since the last report */
  uint32_t dropped_total;
} log_ring;

void log_ring_init(log_ring *r);
/* Adds a whole line or nothing: a line that does not fit is refused and counted. */
bool log_ring_push(log_ring *r, const char *line, size_t len);
/* Takes up to cap bytes, oldest first. Returns the number of bytes taken. */
size_t log_ring_pop(log_ring *r, char *out, size_t cap);

/* A token bucket: up to `burst` lines at once (the start of a stream logs a
 * few dozen within a second), then `per_second` lines a second. A flood (a
 * decoder error on every frame, a loss message on every packet) is cut, and
 * the next line that passes reports how many were left out. */
typedef struct {
  uint32_t burst;           /* bucket size, lines */
  uint32_t per_second;      /* refill, lines a second */
  uint32_t milli_tokens;    /* lines available x 1000 */
  uint64_t last_us;
  bool started;
  uint32_t suppressed;      /* left out since the last line that passed */
  uint32_t suppressed_total;
} log_budget;

/* True when a line may be written now. When lines were left out before it,
 * *report receives their number (else 0). */
bool log_budget_take(log_budget *b, uint64_t now_us, uint32_t *report);

/* The same message again and again is written once, then counted. */
#define LOG_REPEAT_KEY 192

typedef struct {
  char last[LOG_REPEAT_KEY];
  uint32_t repeats;         /* copies of `last` not written */
  uint64_t last_seen_us;
} log_repeat;

/* True when msg repeats the previous message: count it, do not write it.
 * When msg differs and copies of the previous one were held back, *report
 * receives their number (to be written before msg). */
bool log_repeat_check(log_repeat *r, const char *msg, uint64_t now_us, uint32_t *report);
/* Copies held back for longer than max_age_us are reported (and forgotten):
 * returns their number, 0 if none. Called by the writer once a second, so a
 * flood that stops is reported even if no other line follows. */
uint32_t log_repeat_expire(log_repeat *r, uint64_t now_us, uint64_t max_age_us);
