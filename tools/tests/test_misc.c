/*
 * Small helpers: URL encoding of the pairing name (libgamestream/url.c), the
 * percentiles of the session summary (src/video/histogram.h) and the
 * decisions of the automatic reconnection (src/reconnect.h).
 */
#include "test.h"
#include "url.h"
#include "histogram.h"
#include "reconnect.h"

#include <string.h>

int main(void) {
  TEST_BEGIN("url, percentiles and reconnection");

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

  return TEST_END();
}
