/*
 * Press-to-picture test (src/video/latency_logic.h) against a simulated PC
 * and stream: the test page turns white at the PC's next frame after the
 * press reaches it, Sunshine captures at 60 fps, the decoder holds three
 * pictures, the display flips at the next refresh. A full run must end with
 * every press measured and the parts adding up; a page that is not there, or
 * that never reacts, must end the test with the right reason.
 */
#include "test.h"
#include "latency_logic.h"

#include <stdlib.h>
#include <string.h>

#define MS 1000ull
#define PC_FRAME_US 16667ull     /* PC screen and capture: 60 Hz */
#define VITA_FRAME_US 16683ull   /* Vita screen: 59.94 Hz */
#define NET_US (5 * MS)          /* press to the PC, and first packet to the Vita */
#define ENCODE_US (6 * MS)
#define HOLD 3                   /* pictures kept by the decoder */

typedef enum { PAGE_WORKS, PAGE_ABSENT_WHITE, PAGE_NO_REACTION, PAGE_STUCK } page_kind;

typedef struct {
  uint64_t press_at_pc;          /* the press reached the PC (0: released) */
  bool held;
} pc_state;

/* Runs the test for up to `seconds`; returns the final state. */
static lt_state run(page_kind kind, int seconds, uint64_t *expected_mean) {
  static lt_state s;
  memset(&s, 0, sizeof(s));
  uint64_t t0 = 10 * 1000 * MS;
  lt_start(&s, t0, 12345);

  /* Pictures in flight: captured luma, capture time, first packet, decode. */
  enum { MAXF = 4000 };
  static int luma[MAXF];
  static uint64_t rx[MAXF];
  int captured = 0, decoded_in = 0;
  bool a_sent = false;
  uint64_t a_change_at_pc = 0;   /* when the last A change reaches the PC */
  bool a_at_pc = false, a_pending = false, a_pending_value = false;
  bool white = false;            /* what the page shows now */
  uint64_t next_input = t0, next_capture = t0, next_vblank = t0 + 3000;
  int pending_show = -1;         /* newest decoded frame waiting for the next refresh */
  uint64_t sum_expected = 0;
  int n_expected = 0;
  uint64_t press_sent = 0;

  for (uint64_t t = t0; t < t0 + (uint64_t)seconds * 1000 * MS; t += 100) {
    /* input thread: once per Vita sample */
    if (t >= next_input) {
      next_input += VITA_FRAME_US;
      bool want = lt_tick(&s, t);
      if (want != a_sent) {
        a_sent = want;
        lt_on_sent(&s, want, t);
        a_pending = true;
        a_pending_value = want;
        a_change_at_pc = t + NET_US;
        if (want) press_sent = t;
      }
      if (!lt_running(&s))
        break;
    }
    if (a_pending && t >= a_change_at_pc) {
      a_at_pc = a_pending_value;
      a_pending = false;
    }
    /* the page: reacts at the PC's next frame */
    if (t % PC_FRAME_US < 100) {
      switch (kind) {
        case PAGE_WORKS: white = a_at_pc; break;
        case PAGE_ABSENT_WHITE: white = true; break;
        case PAGE_NO_REACTION: white = false; break;
        case PAGE_STUCK: white = white || a_at_pc; break;
      }
    }
    /* capture and network */
    if (t >= next_capture && captured < MAXF) {
      next_capture += PC_FRAME_US;
      luma[captured] = white ? 235 : 16;
      rx[captured] = t + ENCODE_US + NET_US;
      captured++;
    }
    /* decode: frame k comes out when frame k + HOLD has been decoded (3 ms) */
    while (decoded_in < captured && t >= rx[decoded_in] + 3 * MS) {
      int out = decoded_in - HOLD;
      if (out >= 0) {
        lt_on_decoded(&s, out, luma[out], t, rx[out], ENCODE_US);
        pending_show = out;
      }
      decoded_in++;
    }
    /* display: the newest decoded picture at each refresh */
    if (t >= next_vblank) {
      next_vblank += VITA_FRAME_US;
      if (pending_show >= 0) {
        if (luma[pending_show] > 200 && press_sent && kind == PAGE_WORKS &&
            lt_get_phase(&s) == LT_PRESSED && s.obs_visible_us == 0) {
          sum_expected += t - press_sent;
          n_expected++;
        }
        lt_on_visible(&s, pending_show, t);
        pending_show = -1;
      }
    }
  }
  *expected_mean = n_expected ? sum_expected / (uint64_t)n_expected : 0;
  return s;
}

int main(void) {
  TEST_BEGIN("press-to-picture test");

  uint64_t expected_mean;
  lt_state s = run(PAGE_WORKS, 120, &expected_mean);
  lt_summary sum = lt_summarize(&s);
  printf("    %d presses measured, %d missed: median %.1f ms (min %.1f, max %.1f, mean %.1f; simulated mean %.1f)\n",
         sum.count, s.missed, sum.total_median_us / 1000.0, sum.total_min_us / 1000.0,
         sum.total_max_us / 1000.0, sum.total_mean_us / 1000.0, expected_mean / 1000.0);
  printf("    parts: to packet %.1f (PC %.1f), to decoded %.1f, to screen %.1f ms\n",
         sum.to_packet_us / 1000.0, sum.host_us / 1000.0, sum.to_decoded_us / 1000.0, sum.to_screen_us / 1000.0);
  CHECK(lt_get_phase(&s) == LT_DONE, "full run: the test ends");
  CHECK(sum.count == LT_PRESSES && s.missed == 0, "full run: every press measured, none missed");
  CHECK(s.done == LT_PRESSES + LT_WARMUP, "full run: the warm-up presses are not counted");
  CHECK(sum.total_min_us <= sum.total_median_us && sum.total_median_us <= sum.total_max_us,
        "full run: min <= median <= max");
  /* Network 5 + page 0-17 + capture 0-17 + encode 6 + network 5 + hold 3 x 16.7
   * + decode 3 + refresh 0-17: about 90 to 140 ms. */
  CHECK(sum.total_min_us >= 75 * MS && sum.total_max_us <= 150 * MS, "full run: every total within the simulated chain");
  long diff = (long)sum.total_mean_us - (long)expected_mean;
  CHECK(diff > -2000 && diff < 2000, "full run: the mean matches the simulated delay");
  CHECK(sum.parts_count == sum.count, "full run: every first packet known");
  long parts = (long)sum.to_packet_us + (long)sum.to_decoded_us + (long)sum.to_screen_us;
  CHECK(labs(parts - (long)sum.total_median_us) < 20000, "full run: the parts add up to about the total");
  CHECK(sum.host_us == ENCODE_US, "full run: the PC's time is the one reported");
  CHECK(!s.want_press, "full run: A released at the end");

  s = run(PAGE_ABSENT_WHITE, 20, &expected_mean);
  CHECK(lt_get_phase(&s) == LT_FAILED && s.fail == LT_FAIL_NOT_DARK, "white picture from the start: page not on screen");
  CHECK(s.done == 0, "white picture from the start: no press made");

  s = run(PAGE_NO_REACTION, 60, &expected_mean);
  CHECK(lt_get_phase(&s) == LT_FAILED && s.fail == LT_FAIL_NO_REACTION, "page that never turns white: no reaction");
  CHECK(s.missed == LT_WARMUP && s.done == LT_WARMUP,
        "page that never turns white: stops after the two warm-up presses (a game gets no more)");

  s = run(PAGE_STUCK, 60, &expected_mean);
  CHECK(lt_get_phase(&s) == LT_FAILED && s.fail == LT_FAIL_STUCK_WHITE, "page that stays white: stopped");

  /* Stopping in the middle releases A. */
  memset(&s, 0, sizeof(s));
  lt_start(&s, 1000, 7);
  lt_stop(&s);
  CHECK(lt_get_phase(&s) == LT_FAILED && s.fail == LT_FAIL_STOPPED && !lt_tick(&s, 5000000),
        "stopped: A is no longer held");
  CHECK(!lt_running(&s), "stopped: not running");

  /* Summary of hand-made presses: median of an even count, unknown packets. */
  memset(&s, 0, sizeof(s));
  uint64_t totals[4] = {100, 80, 120, 90};
  for (int i = 0; i < 4; i++) {
    s.results[i].sent_us = 1000 * MS;
    s.results[i].decoded_us = s.results[i].sent_us + (totals[i] - 10) * MS;
    s.results[i].visible_us = s.results[i].sent_us + totals[i] * MS;
    s.results[i].rx_us = i == 0 ? 0 : s.results[i].sent_us + 40 * MS;
  }
  s.measured = 4;
  sum = lt_summarize(&s);
  CHECK(sum.total_median_us == 95 * MS && sum.total_min_us == 80 * MS && sum.total_max_us == 120 * MS,
        "summary: median of four, min, max");
  CHECK(sum.parts_count == 3 && sum.to_packet_us == 40 * MS && sum.to_screen_us == 10 * MS,
        "summary: parts only from the presses whose packet is known");
  s.measured = 0;
  sum = lt_summarize(&s);
  CHECK(sum.count == 0 && sum.total_min_us == 0, "summary: nothing measured");

  return TEST_END();
}
