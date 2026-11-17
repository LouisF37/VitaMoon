#ifndef LATENCY_LOGIC_H
#define LATENCY_LOGIC_H

/*
 * Press-to-picture test (quick menu > Measure the delay): the state machine,
 * kept free of Vita calls so that a whole run is simulated on a PC
 * (tools/tests/test_latency.c). src/video/latency_test.c connects it to the
 * threads of the stream.
 *
 * The PC shows tools/latency/latency.html: black, white while the controller's
 * A button is held. VitaMoon presses A on its own (the input thread sends it),
 * watches the centre of each decoded picture, and notes for each press:
 *   sent      the input thread sent the press to the PC
 *   rx        the first packet of the first white picture arrived
 *   decoded   the decoder handed that picture back
 *   visible   it went on screen (as in the statistics: the refresh after the
 *             flip, or the flip itself with "Show frames at once")
 * LT_WARMUP presses are made first and not counted (the PC's controller may
 * need one to wake up), then LT_PRESSES are measured.
 *
 * Threads: lt_tick and lt_on_sent run on the input thread, lt_on_decoded on
 * the decode thread, lt_on_visible on the display thread. Each observation
 * field has one writer; the phase is written by the input thread only.
 */

#include <stdbool.h>
#include <stdint.h>

#define LT_PRESSES            20
#define LT_WARMUP             2
#define LT_DARK_MAX           70        /* centre luma of the black page (limited range: 16) */
#define LT_BRIGHT_MIN         150       /* ... of the white page (limited range: 235) */
#define LT_SETTLE_US          1500000   /* after the start: the menu closes, buttons are released */
#define LT_DARK_WAIT_US       3000000   /* longest wait for the black page */
#define LT_GAP_MIN_US         250000    /* pause before a press: 250 to 550 ms, at random */
#define LT_GAP_SPAN_US        300000
#define LT_TIMEOUT_US         1500000   /* a press with no white picture by then is missed */
#define LT_MAX_MISSES         5         /* in all; both warm-up presses missed also stop the test */

typedef enum {
  LT_IDLE,
  LT_SETTLE,      /* waiting for the start delay, then for the black page */
  LT_ARMED,       /* black: the next press is due at t_next */
  LT_PRESSED,     /* A held: waiting for the white picture on screen */
  LT_RELEASED,    /* A released: waiting for the black page again */
  LT_DONE,
  LT_FAILED,
} lt_phase;

typedef enum {
  LT_FAIL_NONE,
  LT_FAIL_NOT_DARK,     /* the picture never turned black: test page not on screen */
  LT_FAIL_NO_REACTION,  /* too many presses without a white picture */
  LT_FAIL_STUCK_WHITE,  /* the page stayed white after the release */
  LT_FAIL_STOPPED,      /* stopped from the menu or by a pause */
} lt_fail;

typedef struct {
  uint64_t sent_us, rx_us, decoded_us, visible_us;
  uint32_t host_us;     /* the PC's capture and encoding time for that picture (0: unknown) */
} lt_press;

typedef struct {
  lt_phase phase;
  lt_fail fail;
  int done;             /* presses finished, warm-up included */
  int measured;         /* presses measured (warm-up excluded) */
  int missed;
  uint64_t t_start, t_next;
  uint32_t rng;
  bool want_press;
  lt_press results[LT_PRESSES];
  /* Observations of the press in progress (one writer each). */
  uint64_t obs_sent_us;       /* input thread */
  int obs_white_frame;        /* decode thread: -1 until the white picture is decoded */
  uint64_t obs_rx_us, obs_decoded_us;
  uint32_t obs_host_us;
  uint64_t obs_visible_us;    /* display thread */
  int last_luma;              /* decode thread: centre of the latest decoded picture, -1 = none yet */
  uint64_t last_luma_us;
} lt_state;

static inline lt_phase lt_get_phase(const lt_state *s) {
  return (lt_phase)__atomic_load_n(&s->phase, __ATOMIC_ACQUIRE);
}

static inline void lt_set_phase(lt_state *s, lt_phase p) {
  __atomic_store_n(&s->phase, p, __ATOMIC_RELEASE);
}

static inline bool lt_running(const lt_state *s) {
  lt_phase p = lt_get_phase(s);
  return p != LT_IDLE && p != LT_DONE && p != LT_FAILED;
}

static inline uint32_t lt_random(lt_state *s) {
  s->rng = s->rng * 1664525u + 1013904223u;
  return s->rng >> 8;
}

static inline void lt_start(lt_state *s, uint64_t now, uint32_t seed) {
  s->fail = LT_FAIL_NONE;
  s->done = s->measured = s->missed = 0;
  s->t_start = now;
  s->t_next = now + LT_SETTLE_US;
  s->rng = seed ? seed : 1;
  s->want_press = false;
  __atomic_store_n(&s->last_luma, -1, __ATOMIC_RELAXED);
  __atomic_store_n(&s->obs_white_frame, -1, __ATOMIC_RELAXED);
  lt_set_phase(s, LT_SETTLE);
}

static inline void lt_stop(lt_state *s) {
  if (lt_running(s)) {
    s->want_press = false;
    s->fail = LT_FAIL_STOPPED;
    lt_set_phase(s, LT_FAILED);
  }
}

static inline bool lt_is_dark(const lt_state *s) {
  int luma = __atomic_load_n(&s->last_luma, __ATOMIC_ACQUIRE);
  return luma >= 0 && luma <= LT_DARK_MAX;
}

static inline void lt_arm(lt_state *s, uint64_t now) {
  s->t_next = now + LT_GAP_MIN_US + lt_random(s) % LT_GAP_SPAN_US;
  lt_set_phase(s, LT_ARMED);
}

/* Ends the press in progress: kept if its times hold together. */
static inline void lt_finish_press(lt_state *s, uint64_t now) {
  lt_press p;
  p.sent_us = __atomic_load_n(&s->obs_sent_us, __ATOMIC_ACQUIRE);
  p.visible_us = __atomic_load_n(&s->obs_visible_us, __ATOMIC_ACQUIRE);
  p.rx_us = __atomic_load_n(&s->obs_rx_us, __ATOMIC_ACQUIRE);
  p.decoded_us = __atomic_load_n(&s->obs_decoded_us, __ATOMIC_ACQUIRE);
  p.host_us = __atomic_load_n(&s->obs_host_us, __ATOMIC_ACQUIRE);
  bool ok = p.sent_us != 0 && p.visible_us != 0 && p.decoded_us >= p.sent_us && p.visible_us >= p.decoded_us &&
            p.visible_us - p.sent_us < LT_TIMEOUT_US;
  if (ok && p.rx_us != 0 && (p.rx_us < p.sent_us || p.rx_us > p.decoded_us))
    p.rx_us = 0;   /* not that picture's packet: the part is left out, the total stands */
  if (!ok) {
    s->missed++;
  } else if (s->done >= LT_WARMUP && s->measured < LT_PRESSES) {
    s->results[s->measured++] = p;
  }
  s->done++;
  s->want_press = false;
  s->t_next = now + LT_DARK_WAIT_US;
  lt_set_phase(s, LT_RELEASED);
}

/* Input thread, once per pass while streaming: advances the test and returns
 * true while A must be held. */
static inline bool lt_tick(lt_state *s, uint64_t now) {
  switch (lt_get_phase(s)) {
    case LT_SETTLE:
      if (now < s->t_next)
        break;
      if (lt_is_dark(s))
        lt_arm(s, now);
      else if (now >= s->t_next + LT_DARK_WAIT_US) {
        s->fail = LT_FAIL_NOT_DARK;
        lt_set_phase(s, LT_FAILED);
      }
      break;
    case LT_ARMED:
      if (now >= s->t_next) {
        __atomic_store_n(&s->obs_sent_us, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&s->obs_rx_us, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&s->obs_decoded_us, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&s->obs_host_us, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&s->obs_visible_us, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&s->obs_white_frame, -1, __ATOMIC_RELAXED);
        s->want_press = true;
        s->t_next = now + LT_TIMEOUT_US;
        lt_set_phase(s, LT_PRESSED);
      }
      break;
    case LT_PRESSED:
      if (__atomic_load_n(&s->obs_visible_us, __ATOMIC_ACQUIRE) != 0 || now >= s->t_next)
        lt_finish_press(s, now);
      break;
    case LT_RELEASED:
      // Both warm-up presses unanswered: not the test page (a game would get
      // the A presses), so the test stops at once.
      if (s->missed >= LT_MAX_MISSES || (s->done == LT_WARMUP && s->missed >= LT_WARMUP)) {
        s->fail = LT_FAIL_NO_REACTION;
        lt_set_phase(s, LT_FAILED);
      } else if (lt_is_dark(s)) {
        if (s->measured >= LT_PRESSES)
          lt_set_phase(s, LT_DONE);
        else
          lt_arm(s, now);
      } else if (now >= s->t_next) {
        s->fail = LT_FAIL_STUCK_WHITE;
        lt_set_phase(s, LT_FAILED);
      }
      break;
    default:
      break;
  }
  return s->want_press && lt_get_phase(s) == LT_PRESSED;
}

/* Input thread: a controller state was sent to the PC at t (A held or not). */
static inline void lt_on_sent(lt_state *s, bool a_held, uint64_t t) {
  if (a_held && lt_get_phase(s) == LT_PRESSED && __atomic_load_n(&s->obs_sent_us, __ATOMIC_RELAXED) == 0)
    __atomic_store_n(&s->obs_sent_us, t, __ATOMIC_RELEASE);
}

/* Decode thread: a picture came out of the decoder at t with this luma at its
 * centre. rx_us / host_us: its first packet and the PC's time for it. */
static inline void lt_on_decoded(lt_state *s, int frame, int luma, uint64_t t, uint64_t rx_us, uint32_t host_us) {
  __atomic_store_n(&s->last_luma, luma, __ATOMIC_RELEASE);
  __atomic_store_n(&s->last_luma_us, t, __ATOMIC_RELAXED);
  if (lt_get_phase(s) != LT_PRESSED || luma < LT_BRIGHT_MIN)
    return;
  if (__atomic_load_n(&s->obs_sent_us, __ATOMIC_ACQUIRE) == 0 ||
      __atomic_load_n(&s->obs_white_frame, __ATOMIC_RELAXED) >= 0)
    return;
  __atomic_store_n(&s->obs_rx_us, rx_us, __ATOMIC_RELAXED);
  __atomic_store_n(&s->obs_host_us, host_us, __ATOMIC_RELAXED);
  __atomic_store_n(&s->obs_decoded_us, t, __ATOMIC_RELAXED);
  __atomic_store_n(&s->obs_white_frame, frame, __ATOMIC_RELEASE);
}

/* Display thread: the picture of this frame number goes on screen at t. A
 * later frame counts too (the white one itself may have been replaced). */
static inline void lt_on_visible(lt_state *s, int frame, uint64_t t) {
  if (lt_get_phase(s) != LT_PRESSED)
    return;
  int white = __atomic_load_n(&s->obs_white_frame, __ATOMIC_ACQUIRE);
  if (white < 0 || frame < white || __atomic_load_n(&s->obs_visible_us, __ATOMIC_RELAXED) != 0)
    return;
  __atomic_store_n(&s->obs_visible_us, t, __ATOMIC_RELEASE);
}

/* ---- summary ---- */

typedef struct {
  int count;
  uint32_t total_median_us, total_min_us, total_max_us, total_mean_us;
  /* medians of the parts; parts_count = presses whose first packet is known */
  int parts_count;
  uint32_t to_packet_us, host_us, to_decoded_us, to_screen_us;
} lt_summary;

static inline uint32_t lt_median(uint32_t *v, int n) {
  for (int i = 1; i < n; i++) {           /* insertion sort: at most LT_PRESSES values */
    uint32_t x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = x;
  }
  if (n == 0)
    return 0;
  return n % 2 ? v[n / 2] : (uint32_t)(((uint64_t)v[n / 2 - 1] + v[n / 2]) / 2);
}

static inline lt_summary lt_summarize(const lt_state *s) {
  lt_summary out = {0};
  uint32_t total[LT_PRESSES] = {0}, a[LT_PRESSES] = {0}, h[LT_PRESSES] = {0}, b[LT_PRESSES] = {0},
           c[LT_PRESSES] = {0};
  uint64_t sum = 0;
  int hn = 0;
  out.count = s->measured < 0 ? 0 : (s->measured > LT_PRESSES ? LT_PRESSES : s->measured);
  out.total_min_us = 0xFFFFFFFFu;
  for (int i = 0; i < out.count; i++) {
    const lt_press *p = &s->results[i];
    total[i] = (uint32_t)(p->visible_us - p->sent_us);
    sum += total[i];
    if (total[i] < out.total_min_us) out.total_min_us = total[i];
    if (total[i] > out.total_max_us) out.total_max_us = total[i];
    if (p->rx_us != 0) {
      a[out.parts_count] = (uint32_t)(p->rx_us - p->sent_us);
      b[out.parts_count] = (uint32_t)(p->decoded_us - p->rx_us);
      out.parts_count++;
    }
    c[i] = (uint32_t)(p->visible_us - p->decoded_us);
    if (p->host_us)
      h[hn++] = p->host_us;
  }
  if (out.count == 0) {
    out.total_min_us = 0;
    return out;
  }
  out.total_mean_us = (uint32_t)(sum / (uint64_t)out.count);
  out.total_median_us = lt_median(total, out.count);
  out.to_packet_us = lt_median(a, out.parts_count);
  out.to_decoded_us = lt_median(b, out.parts_count);
  out.to_screen_us = lt_median(c, out.count);
  out.host_us = lt_median(h, hn);
  return out;
}

#endif // LATENCY_LOGIC_H
