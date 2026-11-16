/*
 * Pure parts of the log: see log_core.h.
 */
#include "log_core.h"

#include <string.h>

void log_ring_init(log_ring *r) {
  r->head = 0;
  r->used = 0;
  r->dropped = 0;
  r->dropped_total = 0;
}

bool log_ring_push(log_ring *r, const char *line, size_t len) {
  if (len == 0)
    return true;
  if (len > LOG_RING_SIZE - r->used) {
    r->dropped++;
    r->dropped_total++;
    return false;
  }
  size_t tail = (r->head + r->used) % LOG_RING_SIZE;
  size_t first = LOG_RING_SIZE - tail;
  if (first > len)
    first = len;
  memcpy(r->buf + tail, line, first);
  memcpy(r->buf, line + first, len - first);
  r->used += len;
  return true;
}

size_t log_ring_pop(log_ring *r, char *out, size_t cap) {
  size_t n = r->used < cap ? r->used : cap;
  size_t first = LOG_RING_SIZE - r->head;
  if (first > n)
    first = n;
  memcpy(out, r->buf + r->head, first);
  memcpy(out + first, r->buf, n - first);
  r->head = (r->head + n) % LOG_RING_SIZE;
  r->used -= n;
  return n;
}

bool log_budget_take(log_budget *b, uint64_t now_us, uint32_t *report) {
  *report = 0;
  uint64_t cap = (uint64_t)b->burst * 1000;
  if (!b->started) {
    b->started = true;
    b->milli_tokens = (uint32_t)cap;
  } else if (now_us > b->last_us) {
    uint64_t add = (now_us - b->last_us) * b->per_second / 1000;   /* milli-tokens */
    uint64_t t = b->milli_tokens + add;
    b->milli_tokens = (uint32_t)(t > cap ? cap : t);
  }
  b->last_us = now_us;
  if (b->milli_tokens < 1000) {
    b->suppressed++;
    b->suppressed_total++;
    return false;
  }
  b->milli_tokens -= 1000;
  *report = b->suppressed;
  b->suppressed = 0;
  return true;
}

bool log_repeat_check(log_repeat *r, const char *msg, uint64_t now_us, uint32_t *report) {
  *report = 0;
  if (strncmp(r->last, msg, LOG_REPEAT_KEY - 1) == 0 && r->last[0]) {
    r->repeats++;
    r->last_seen_us = now_us;
    return true;
  }
  *report = r->repeats;
  r->repeats = 0;
  strncpy(r->last, msg, LOG_REPEAT_KEY - 1);
  r->last[LOG_REPEAT_KEY - 1] = '\0';
  r->last_seen_us = now_us;
  return false;
}

uint32_t log_repeat_expire(log_repeat *r, uint64_t now_us, uint64_t max_age_us) {
  if (r->repeats == 0 || now_us - r->last_seen_us < max_age_us)
    return 0;
  uint32_t n = r->repeats;
  r->repeats = 0;
  /* Forget the message too: if it comes back, it is written again. */
  r->last[0] = '\0';
  return n;
}
