/*
 * Small helpers: URL encoding of the pairing name (libgamestream/url.c), the
 * percentiles of the session summary (src/video/histogram.h) and the
 * decisions of the automatic reconnection (src/reconnect.h).
 */
#include "test.h"
#include "url.h"
#include "histogram.h"
#include "reconnect.h"
#include "device_name.h"
#include "stats_prune.h"

#include <string.h>

int main(void) {
  TEST_BEGIN("url, percentiles, reconnection and PC names");

  char out[64];
  gs_url_encode("PS Vita", out, sizeof(out));
  CHECK(strcmp(out, "PS%20Vita") == 0, "space encoded");
  gs_url_encode("Louis-Vita_2.0~", out, sizeof(out));
  CHECK(strcmp(out, "Louis-Vita_2.0~") == 0, "unreserved characters kept");
  gs_url_encode("Am\xc3\xa9lie&co", out, sizeof(out));
  CHECK(strcmp(out, "Am%C3%A9lie%26co") == 0, "UTF-8 bytes and & encoded");
  size_t n = gs_url_encode("a b", out, 4);
  CHECK(n == 1 && strcmp(out, "a") == 0, "never ends in the middle of an escape");
  n = gs_url_encode(NULL, out, sizeof(out));
  CHECK(n == 0 && out[0] == 0, "NULL gives an empty string");

  static hist_t h = {.step_us = 500};
  hist_reset(&h);
  CHECK(hist_percentile(&h, 50) == 0, "empty histogram: 0");
  for (int i = 1; i <= 100; i++)
    hist_add(&h, (uint32_t)i * 1000);   /* 1..100 ms */
  CHECK(h.n == 100 && h.max == 100000, "count and maximum");
  uint32_t p50 = hist_percentile(&h, 50), p95 = hist_percentile(&h, 95), p99 = hist_percentile(&h, 99);
  printf("    1..100 ms: p50 %u, p95 %u, p99 %u us\n", p50, p95, p99);
  CHECK(p50 == 50500, "p50 is the bucket of 50 ms");
  CHECK(p95 == 95500, "p95 is the bucket of 95 ms");
  CHECK(p99 == 99500, "p99 is the bucket of 99 ms");
  char line[160];
  hist_format(line, sizeof(line), "test", &h);
  CHECK(strstr(line, "p50 50.50 ms, p95 95.50 ms, p99 99.50 ms, max 100.00 ms") != NULL,
        "formatted in milliseconds");
  hist_add(&h, 10000000);   /* 10 s: above the range */
  CHECK(hist_percentile(&h, 100) == UINT32_MAX, "values above the range reported as such");
  hist_format(line, sizeof(line), "test", &h);
  CHECK(strstr(line, "max 10000.00 ms (101 values)") != NULL, "maximum kept beyond the range");

  /* reconnection: which ends are a lost connection */
  CHECK(!reconnect_after_error(ML_ERROR_GRACEFUL_TERMINATION), "ended by the PC or the user: no reconnection");
  CHECK(!reconnect_after_error(ML_ERROR_PROTECTED_CONTENT), "DRM content: no reconnection");
  CHECK(!reconnect_after_error(ML_ERROR_UNEXPECTED_EARLY_TERMINATION), "capture error on the PC: no reconnection");
  CHECK(!reconnect_after_error(ML_ERROR_FRAME_CONVERSION), "picture conversion error: no reconnection");
  CHECK(reconnect_after_error(ML_ERROR_NO_VIDEO_TRAFFIC), "no video traffic: reconnection");
  CHECK(reconnect_after_error(ML_ERROR_NO_VIDEO_FRAME), "no video frame: reconnection");
  CHECK(reconnect_after_error(-1), "control stream lost: reconnection");
  CHECK(reconnect_after_error(110), "socket error: reconnection");

  /* reconnection: streams lost again soon after starting */
  int quick = 0;
  CHECK(reconnect_should_retry(&quick, 600000) && quick == 0, "lost after 10 min: retry, no quick loss");
  CHECK(reconnect_should_retry(&quick, 5000) && quick == 1, "lost after 5 s: retry, 1 quick loss");
  CHECK(reconnect_should_retry(&quick, 2000) && quick == 2, "lost again after 2 s: retry, 2 quick losses");
  CHECK(!reconnect_should_retry(&quick, 1000) && quick == 3, "third quick loss in a row: stop");
  quick = 2;
  CHECK(reconnect_should_retry(&quick, RECONNECT_QUICK_LOSS_MS) && quick == 0,
        "a stream that held 20 s resets the count");

  /* PC names are folder names (device_name.h). */
  CHECK(device_name_valid("MINI-PC") && device_name_valid("Salon PC 2"), "plain names accepted");
  CHECK(!device_name_valid("") && !device_name_valid(".") && !device_name_valid(".."),
        "empty, . and .. refused");
  CHECK(!device_name_valid("a/b") && !device_name_valid("..\\x") && !device_name_valid("pc:1"),
        "path separators and drive colons refused");
  char name[16] = "my/pc:*";
  device_name_sanitize(name, sizeof(name), "PC");
  CHECK(strcmp(name, "my-pc--") == 0, "announced name: forbidden characters become '-'");
  char dots[16] = "..";
  device_name_sanitize(dots, sizeof(dots), "PC");
  CHECK(strcmp(dots, "PC") == 0, "announced name '..' replaced");

  /* Statistics files: the newest STATS_KEEP_SESSIONS streams are kept
   * (stats_prune.h); nothing else in the folder is ever chosen. */
  static char names[80][40];
  const char *list[80];
  bool del[80];
  int files = 0;
  for (int s = 0; s < 35; s++) {   /* 35 streams, written in no order */
    int k = (s * 17) % 35;
    snprintf(names[files], sizeof(names[files]), "session-202611%02d-%06d.txt", 1 + k / 10, k);
    list[files] = names[files]; files++;
    snprintf(names[files], sizeof(names[files]), "session-202611%02d-%06d.csv", 1 + k / 10, k);
    list[files] = names[files]; files++;
  }
  snprintf(names[files], sizeof(names[files]), "last_1005.txt"); list[files] = names[files]; files++;
  snprintf(names[files], sizeof(names[files]), "session-notes.txt"); list[files] = names[files]; files++;
  snprintf(names[files], sizeof(names[files]), "session-20261101-000000.log"); list[files] = names[files]; files++;
  int marked = stats_prune_choose(list, files, STATS_KEEP_SESSIONS, del);
  CHECK(marked == 10, "35 streams: the files of the 5 oldest are chosen (10 files)");
  bool oldest_only = true, others_kept = true;
  for (int i = 0; i < files; i++) {
    const char *stamp = stats_session_stamp(list[i]);
    if (del[i] && (!stamp || strcmp(stamp, "20261101-000005") >= 0))
      oldest_only = false;
    if (!stamp && del[i])
      others_kept = false;
  }
  CHECK(oldest_only, "only the 5 oldest streams are chosen");
  CHECK(others_kept, "per-game summaries and other files are never chosen");
  CHECK(stats_prune_choose(list, 4, STATS_KEEP_SESSIONS, del) == 0, "fewer than 30 streams: nothing chosen");
  return TEST_END();
}
