#ifndef LATENCY_TEST_H
#define LATENCY_TEST_H

/*
 * Press-to-picture test, started from the quick menu while the PC shows
 * tools/latency/latency.html. The state machine and what it measures are
 * described in latency_logic.h; this module connects it to the stream's
 * threads, draws its progress over the picture and writes the result to
 * <data>/stats/latency-<date>.txt.
 */

#include <stdbool.h>
#include <stdint.h>

/* Input thread (quick menu): starts a test, or stops the one running. */
void latency_test_toggle(void);
bool latency_test_running(void);

/* Input thread, once per pass: true while the test holds A. Then, after each
 * controller state sent to the PC, whether A was held in it. */
bool latency_test_input_tick(void);
void latency_test_on_sent(bool a_held, uint64_t t_us);

/* Decode thread: whether pictures must be looked at, then each picture out of
 * the decoder (its texture data: NV21 if yuv, RGBA otherwise). */
bool latency_test_watching(void);
void latency_test_on_decoded(int frame, const void *pixels, bool yuv, int width, int height,
                             uint64_t t_us, uint64_t rx_us, uint32_t host_us);

/* Display thread: a frame goes on screen at t_us; drawing of the progress box. */
void latency_test_on_visible(int frame, uint64_t t_us);
void latency_test_draw(void);

/* Menu thread, before a stream: renders the texts of the progress box. */
void latency_test_prepare_text(void);

/* Statistics thread, once a second: writes the result of a finished test. */
void latency_test_poll(void);

/* The stream pauses or ends: a test in progress stops. */
void latency_test_stop(void);

#endif // LATENCY_TEST_H
