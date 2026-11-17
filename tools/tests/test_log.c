/*
 * The log's ring, per-second budget and repeat folding (src/log_core.c).
 */
#include "test.h"
#include "log_core.h"
#include "log_files.h"

#include <string.h>

static log_ring ring;   /* 64 KiB: not on the stack */

int main(void) {
  TEST_BEGIN("log");

  /* ---- ring ---- */
  log_ring_init(&ring);
  char out[LOG_RING_SIZE];
  CHECK(log_ring_push(&ring, "one\n", 4) && log_ring_push(&ring, "two\n", 4), "lines go in");
  size_t n = log_ring_pop(&ring, out, sizeof(out));
  CHECK(n == 8 && memcmp(out, "one\ntwo\n", 8) == 0, "and come out in order");
  CHECK(ring.used == 0, "the ring is then empty");

  /* Wrap around the end of the buffer. */
  static char big[LOG_RING_SIZE];
  memset(big, 'x', sizeof(big));
  log_ring_init(&ring);
  CHECK(log_ring_push(&ring, big, LOG_RING_SIZE - 10), "a line almost as big as the ring fits");
  n = log_ring_pop(&ring, out, LOG_RING_SIZE - 20);
  CHECK(n == LOG_RING_SIZE - 20 && ring.used == 10, "partial reads leave the rest");
  CHECK(log_ring_push(&ring, "0123456789abcdef\n", 17), "a line across the end of the buffer");
  n = log_ring_pop(&ring, out, sizeof(out));
  CHECK(n == 27 && memcmp(out, "xxxxxxxxxx0123456789abcdef\n", 27) == 0, "comes out whole after the wrap");

  /* Full ring: whole lines refused and counted, nothing half written. */
  log_ring_init(&ring);
  CHECK(log_ring_push(&ring, big, LOG_RING_SIZE - 3), "nearly full");
  CHECK(!log_ring_push(&ring, "four", 4), "a line that does not fit is refused");
  CHECK(ring.used == LOG_RING_SIZE - 3 && ring.dropped == 1 && ring.dropped_total == 1, "counted, nothing written");
  CHECK(log_ring_push(&ring, "ok\n", 3) && ring.used == LOG_RING_SIZE, "a line that fits exactly still goes in");

  /* ---- budget ---- */
  log_budget b = { .burst = 5, .per_second = 2 };
  uint32_t report = 99;
  uint64_t t = 5000000;
  int written = 0;
  for (int i = 0; i < 20; i++)
    if (log_budget_take(&b, t + i * 1000, &report))
      written++;
  CHECK(written == 5, "a burst of five lines passes at once");
  CHECK(b.suppressed == 15 && b.suppressed_total == 15, "the rest of the flood is counted");
  CHECK(!log_budget_take(&b, t + 400000, &report), "0.4 s later: still nothing (2 lines a second)");
  CHECK(log_budget_take(&b, t + 520000, &report) && report == 16, "half a second refills one line, which reports the 16");
  CHECK(!log_budget_take(&b, t + 530000, &report) && report == 0, "the bucket is empty again");
  CHECK(log_budget_take(&b, t + 1100000, &report) && report == 1, "the next line reports the one left out");
  int after_pause = 0;
  for (int i = 0; i < 20; i++)
    if (log_budget_take(&b, t + 60000000 + i, &report))
      after_pause++;
  CHECK(after_pause == 5, "after a quiet minute the bucket holds a full burst, no more");
  CHECK(!log_budget_take(&b, t + 1000, &report), "a clock that goes back refills nothing");

  /* ---- repeats ---- */
  log_repeat r = {0};
  CHECK(!log_repeat_check(&r, "mcc  waiting for IDR", 1000, &report) && report == 0, "a first message is written");
  CHECK(log_repeat_check(&r, "mcc  waiting for IDR", 2000, &report), "the same again is held back");
  CHECK(log_repeat_check(&r, "mcc  waiting for IDR", 3000, &report) && r.repeats == 2, "and counted");
  CHECK(!log_repeat_check(&r, "video  decoder ok", 4000, &report) && report == 2,
        "another message is written, after the count of copies");
  CHECK(log_repeat_expire(&r, 5000, 2000000) == 0, "nothing held back: nothing to report");
  log_repeat_check(&r, "video  decoder ok", 5000, &report);
  CHECK(log_repeat_expire(&r, 1000000, 2000000) == 0, "copies are kept while the flood may go on");
  CHECK(log_repeat_expire(&r, 2005000, 2000000) == 1, "and reported once it has stopped");
  CHECK(!log_repeat_check(&r, "video  decoder ok", 2006000, &report) && report == 0,
        "after that report the message is written again");

  /* Log files: one per run, dated; the newest LOG_KEEP_RUNS kept (log_files.h) */
  char name[64];
  log_file_name(name, sizeof(name), 2026, 11, 17, 0, 53, 7);
  CHECK(strcmp(name, "vitamoon-20261117-005307.log") == 0, "a run's log is named after its start");
  CHECK(log_file_stamp(name) && strncmp(log_file_stamp(name), "20261117-005307", 15) == 0,
        "its stamp is read back");
  CHECK(!log_file_stamp("vitamoon.log") && !log_file_stamp("vitamoon.1.log") &&
            !log_file_stamp("vitamoon-2026111-005307.log") && !log_file_stamp("vitamoon-20261117-005307.txt") &&
            !log_file_stamp("vitamoon-20261117x005307.log") && !log_file_stamp("session-20261117-005307.log") &&
            !log_file_stamp("vitamoon-2026111a-005307.log"),
        "other names are not logs of a run");
  static char names[64][48];
  const char *list[64];
  bool del[64];
  int files = 0;
  for (int r = 0; r < 33; r++) {   /* 33 runs, listed in no order */
    int k = (r * 7) % 33;
    log_file_name(names[files], sizeof(names[files]), 2026, 11, 1 + k / 10, k % 10, 0, 0);
    list[files] = names[files]; files++;
  }
  snprintf(names[files], sizeof(names[files]), "notes.txt"); list[files] = names[files]; files++;
  snprintf(names[files], sizeof(names[files]), "vitamoon.log"); list[files] = names[files]; files++;
  int marked = log_prune_choose(list, files, LOG_KEEP_RUNS - 1, del);
  bool right = marked == 4;
  for (int i = 0; i < files; i++) {
    const char *stamp = log_file_stamp(list[i]);
    bool oldest = stamp && strncmp(stamp, "20261101-04", 11) < 0;   /* runs 0 to 3 */
    if (del[i] != oldest)
      right = false;
  }
  CHECK(right, "33 runs: the 4 oldest are deleted to keep 29 and make room for the new one");
  bool others_kept = true;
  for (int i = 0; i < files; i++)
    if (!log_file_stamp(list[i]) && del[i])
      others_kept = false;
  CHECK(others_kept, "nothing else in the folder is ever deleted");

  return TEST_END();
}
