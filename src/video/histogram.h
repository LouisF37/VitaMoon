/*
 * Fixed-bucket histograms for the end-of-session percentiles of the stream
 * statistics (stream_stats.c). Header-only so that tools/tests can check the
 * percentile arithmetic on a PC.
 */
#ifndef VIDEO_HISTOGRAM_H
#define VIDEO_HISTOGRAM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Averages hide stutters: the summary of each session also gets the 50th, 95th
 * and 99th percentiles (end-of-session percentiles, as dubleyu/
 * vita-moonlight-relay reports). One writer thread per histogram; the counts
 * are updated atomically so a reader never sees a torn value. */
#define HIST_BUCKETS 512
#define HIST_GAP_US 1000000   /* longer gaps between shown frames: a pause, not counted */

typedef struct {
  uint32_t step_us;
  uint32_t count[HIST_BUCKETS + 1];   /* the last bucket holds everything above the range */
  uint32_t n;
  uint32_t max;
} hist_t;

static inline void hist_add(hist_t *h, uint32_t v) {
  uint32_t b = v / h->step_us;
  if (b > HIST_BUCKETS)
    b = HIST_BUCKETS;
  __atomic_fetch_add(&h->count[b], 1, __ATOMIC_RELAXED);
  __atomic_fetch_add(&h->n, 1, __ATOMIC_RELAXED);
  uint32_t cur = __atomic_load_n(&h->max, __ATOMIC_RELAXED);
  while (v > cur &&
         !__atomic_compare_exchange_n(&h->max, &cur, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
  }
}

static inline void hist_reset(hist_t *h) {
  memset(h->count, 0, sizeof(h->count));
  h->n = 0;
  h->max = 0;
}

/* Upper bound of the bucket that holds the pct-th percentile, in microseconds;
 * UINT32_MAX when it lies above the range. */
static inline uint32_t hist_percentile(const hist_t *h, uint32_t pct) {
  if (h->n == 0)
    return 0;
  uint64_t rank = ((uint64_t)h->n * pct + 99) / 100;
  if (rank == 0)
    rank = 1;
  uint64_t seen = 0;
  for (int b = 0; b <= HIST_BUCKETS; b++) {
    seen += h->count[b];
    if (seen >= rank)
      return b == HIST_BUCKETS ? UINT32_MAX : (uint32_t)(b + 1) * h->step_us;
  }
  return UINT32_MAX;
}

static inline void hist_format(char *out, size_t n, const char *name, const hist_t *h) {
  char p[3][16];
  const uint32_t pcts[3] = {50, 95, 99};
  for (int i = 0; i < 3; i++) {
    uint32_t v = hist_percentile(h, pcts[i]);
    if (v == UINT32_MAX)
      snprintf(p[i], sizeof(p[i]), "> %u", (unsigned)(HIST_BUCKETS * h->step_us / 1000));
    else
      snprintf(p[i], sizeof(p[i]), "%u.%02u", (unsigned)(v / 1000), (unsigned)(v % 1000 / 10));
  }
  snprintf(out, n, "  %-32s p50 %s ms, p95 %s ms, p99 %s ms, max %u.%02u ms (%u values)\n", name,
           p[0], p[1], p[2], (unsigned)(h->max / 1000), (unsigned)(h->max % 1000 / 10), (unsigned)h->n);
}

#endif
