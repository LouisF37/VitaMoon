/*
 * Press-to-picture test (latency_test.h): threads, drawing and result file.
 * The decisions are in latency_logic.h (tested on a PC).
 */
#include "latency_test.h"
#include "latency_logic.h"

#include "../app_info.h"
#include "../config.h"
#include "../i18n.h"
#include "../log.h"
#include "../gui/theme.h"

#include <Limelight.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>

#include <stdio.h>
#include <string.h>

#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

static lt_state st;
static volatile int generation = 0;     /* tests started (input thread) */
static int written_generation = 0;      /* tests whose result is written (statistics thread) */

/* ---- input thread ---- */

void latency_test_toggle(void) {
  if (lt_running(&st)) {
    lt_stop(&st);
    LOGI("latency", "press-to-picture test stopped from the quick menu");
    return;
  }
  uint64_t now = LiGetMicroseconds();
  lt_start(&st, now, (uint32_t)now ^ 0x9E3779B9u);
  generation++;
  LOGI("latency", "press-to-picture test started (%d presses after %d warm-up ones)", LT_PRESSES, LT_WARMUP);
}

bool latency_test_running(void) {
  return lt_running(&st);
}

bool latency_test_input_tick(void) {
  if (lt_get_phase(&st) == LT_IDLE)
    return false;
  return lt_tick(&st, LiGetMicroseconds());
}

void latency_test_on_sent(bool a_held, uint64_t t_us) {
  lt_on_sent(&st, a_held, t_us);
}

void latency_test_stop(void) {
  lt_stop(&st);
}

/* ---- decode thread ---- */

bool latency_test_watching(void) {
  return lt_running(&st);
}

/* Average brightness (0-255) of a 16 x 16 block at the centre of the picture,
 * one pixel in two each way. NV21: the Y plane comes first. RGBA: weighted
 * R, G and B (BT.601). */
static int centre_luma(const uint8_t *pixels, bool yuv, int width, int height) {
  int x0 = width / 2 - 8, y0 = height / 2 - 8;
  if (x0 < 0 || y0 < 0)
    return -1;
  uint32_t sum = 0, n = 0;
  for (int y = y0; y < y0 + 16; y += 2) {
    for (int x = x0; x < x0 + 16; x += 2) {
      if (yuv) {
        sum += pixels[(size_t)y * width + x];
      } else {
        const uint8_t *p = pixels + ((size_t)y * width + x) * 4;
        sum += (77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8;
      }
      n++;
    }
  }
  return (int)(sum / n);
}

void latency_test_on_decoded(int frame, const void *pixels, bool yuv, int width, int height,
                             uint64_t t_us, uint64_t rx_us, uint32_t host_us) {
  if (pixels == NULL || !lt_running(&st))
    return;
  int luma = centre_luma((const uint8_t *)pixels, yuv, width, height);
  if (luma >= 0)
    lt_on_decoded(&st, frame, luma, t_us, rx_us, host_us);
}

/* ---- display thread ---- */

void latency_test_on_visible(int frame, uint64_t t_us) {
  lt_on_visible(&st, frame, t_us);
}

#define BOX_SHOW_RESULT_US 20000000   /* the result stays on screen 20 s */

static const char *fail_text(lt_fail fail) {
  switch (fail) {
    case LT_FAIL_NOT_DARK: return tr("Delay test stopped: the test page is not on the PC screen.");
    case LT_FAIL_NO_REACTION: return tr("Delay test stopped: the page does not react to the A button.");
    case LT_FAIL_STUCK_WHITE: return tr("Delay test stopped: the page stayed white.");
    default: return tr("Delay test stopped.");
  }
}

void latency_test_draw(void) {
  static int drawn_generation = 0;
  static uint64_t ended_at = 0;
  lt_phase phase = lt_get_phase(&st);
  if (phase == LT_IDLE)
    return;
  int gen = generation;
  if (gen != drawn_generation) {
    drawn_generation = gen;
    ended_at = 0;
  }
  uint64_t now = LiGetMicroseconds();
  bool running = phase != LT_DONE && phase != LT_FAILED;
  if (!running) {
    if (ended_at == 0)
      ended_at = now;
    if (now - ended_at > BOX_SHOW_RESULT_US)
      return;
  }

  char line1[160], line2[160];
  line2[0] = 0;
  if (running) {
    if (st.done < LT_WARMUP)
      snprintf(line1, sizeof(line1), "%s", tr("Delay test: getting ready..."));
    else
      snprintf(line1, sizeof(line1), tr("Delay test: press %d of %d"),
               st.measured + 1 > LT_PRESSES ? LT_PRESSES : st.measured + 1, LT_PRESSES);
    if (st.measured > 0 && st.measured <= LT_PRESSES) {
      const lt_press *p = &st.results[st.measured - 1];
      snprintf(line2, sizeof(line2), tr("Last: %.0f ms. Do not touch the controls."),
               (p->visible_us - p->sent_us) / 1000.0);
    } else {
      snprintf(line2, sizeof(line2), "%s", tr("Do not touch the controls."));
    }
  } else if (phase == LT_DONE) {
    lt_summary sum = lt_summarize(&st);
    snprintf(line1, sizeof(line1), tr("Press to picture: %.0f ms (median of %d presses, %.0f to %.0f)"),
             sum.total_median_us / 1000.0, sum.count, sum.total_min_us / 1000.0, sum.total_max_us / 1000.0);
    snprintf(line2, sizeof(line2), "%s", tr("Details saved in the stats folder (latency file)."));
  } else {
    snprintf(line1, sizeof(line1), "%s", fail_text(st.fail));
  }

  const float x = 24, w = 720, h = line2[0] ? 62 : 40;
  const float y = 544 - 24 - h;
  theme_set_alpha(1.0f);
  theme_glass(x, y, w, h, 12);
  theme_text_fit(x + 16, y + 26, phase == LT_FAILED ? TH_WARN : TH_TEXT, TS_BODY, line1, (int)w - 32);
  if (line2[0])
    theme_text_fit(x + 16, y + 49, TH_TEXT2, TS_SMALL, line2, (int)w - 32);
}

/* Menu thread, before a stream: the box's texts are rendered now, so that the
 * display thread draws them without rasterising (docs/design/architecture.md). */
void latency_test_prepare_text(void) {
  static const char *const body[] = {
    "Delay test stopped: the test page is not on the PC screen.",
    "Delay test stopped: the page does not react to the A button.",
    "Delay test stopped: the page stayed white.",
    "Delay test stopped.",
    "Delay test: getting ready...",
    "Delay test: press %d of %d",
    "Press to picture: %.0f ms (median of %d presses, %.0f to %.0f)",
  };
  static const char *const small[] = {
    "Last: %.0f ms. Do not touch the controls.",
    "Do not touch the controls.",
    "Details saved in the stats folder (latency file).",
  };
  for (unsigned i = 0; i < sizeof(body) / sizeof(body[0]); i++)
    theme_prepare_text(TS_BODY, tr(body[i]));
  for (unsigned i = 0; i < sizeof(small) / sizeof(small[0]); i++)
    theme_prepare_text(TS_SMALL, tr(small[i]));
  theme_prepare_text(TS_BODY, "0123456789 ...\xE2\x80\xA6");
  theme_prepare_text(TS_SMALL, "0123456789 ...\xE2\x80\xA6");
}

/* ---- statistics thread: result file ---- */

static void write_str(SceUID fd, const char *s) {
  sceIoWrite(fd, s, strlen(s));
}

static void write_result(void) {
  lt_phase phase = lt_get_phase(&st);
  lt_summary sum = lt_summarize(&st);
  static const char *const fail_names[] = {"", "test page not on screen (never black)",
                                           "no reaction to the A button", "page stayed white", "stopped"};
  int fail = (int)st.fail;
  const char *why = fail >= 0 && fail <= (int)LT_FAIL_STOPPED ? fail_names[fail] : "";
  if (phase == LT_DONE)
    LOGI("latency", "press to picture: median %.1f ms, mean %.1f, min %.1f, max %.1f over %d presses (%d missed); "
         "to first packet %.1f (PC %.1f), to decoded %.1f, to screen %.1f",
         sum.total_median_us / 1000.0, sum.total_mean_us / 1000.0, sum.total_min_us / 1000.0,
         sum.total_max_us / 1000.0, sum.count, st.missed, sum.to_packet_us / 1000.0, sum.host_us / 1000.0,
         sum.to_decoded_us / 1000.0, sum.to_screen_us / 1000.0);
  else
    LOGW("latency", "press-to-picture test ended early: %s (%d presses measured, %d missed)", why, sum.count,
         st.missed);
  if (sum.count == 0)
    return;

  char dir[512], path[640];
  snprintf(dir, sizeof(dir), "%.400s/stats", config.key_dir);
  sceIoMkdir(dir, 0777);
  SceDateTime t;
  sceRtcGetCurrentClockLocalTime(&t);
  snprintf(path, sizeof(path), "%s/latency-%04d%02d%02d-%02d%02d%02d.txt", dir, t.year, t.month, t.day,
           t.hour, t.minute, t.second);
  SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) {
    LOGW("latency", "cannot write %s (0x%08x)", path, (unsigned int)fd);
    return;
  }
  char line[640];
  snprintf(line, sizeof(line),
           APP_NAME " press-to-picture test\n"
           "build: %s\n"
           "date: %04d-%02d-%02d %02d:%02d:%02d\n"
           "stream: %dx%d @ %d fps, bitrate %d kbps, display mode %d (0 direct, 1 low latency, 2 smooth, 3 paced)\n"
           "settings: decoder_catchup=%d yuv_decode=%d reorder_wait_ms=%d low_latency_audio=%d\n"
           "tests: decoder_delay=%d decoder_refs=%d skip_late=%d immediate_flip=%d audio_5ms=%d\n\n",
           BUILD_ID, t.year, t.month, t.day, t.hour, t.minute, t.second,
           config.stream.width, config.stream.height, config.stream.fps, config_stream_bitrate(&config), config.display_mode,
           config.decoder_catchup, config.decode_yuv, config.video_reorder_wait_ms, config.low_latency_audio,
           config.test_decoder_delay, config.test_decoder_refs, config.test_skip_late, config.test_immediate_flip,
           config.test_audio_5ms);
  write_str(fd, line);
  snprintf(line, sizeof(line),
           "result: %d presses measured (%d warm-up presses before them not counted), %d missed%s%s\n"
           "total, press sent -> picture on screen: median %.1f ms, mean %.1f, min %.1f, max %.1f\n"
           "parts (medians): press sent -> first packet of the white picture %.1f ms (of which the PC's\n"
           "  capture and encoding %.1f ms), first packet -> picture out of the decoder %.1f ms,\n"
           "  out of the decoder -> on screen %.1f ms\n\n",
           sum.count, LT_WARMUP, st.missed, phase == LT_DONE ? "" : ", ended early: ", phase == LT_DONE ? "" : why,
           sum.total_median_us / 1000.0, sum.total_mean_us / 1000.0, sum.total_min_us / 1000.0,
           sum.total_max_us / 1000.0, sum.to_packet_us / 1000.0, sum.host_us / 1000.0, sum.to_decoded_us / 1000.0,
           sum.to_screen_us / 1000.0);
  write_str(fd, line);
  write_str(fd,
            "What the total includes: the press's trip to the PC, the test page's reaction (the browser\n"
            "draws at the PC screen's next refresh), the capture, the encoding, the trip back, the\n"
            "decoder and the display. A game has the same chain, with its own reaction time instead of\n"
            "the page's. Not included: a real press waits 0 to 17 ms (8 on average) for the Vita to read\n"
            "its buttons; \"on screen\" is the refresh that shows the picture (as in the session\n"
            "statistics), whose centre the screen draws about 8 ms after its top line.\n\n"
            "press;total_ms;to_packet_ms;pc_ms;to_decoded_ms;to_screen_ms\n");
  for (int i = 0; i < sum.count; i++) {
    const lt_press *p = &st.results[i];
    if (p->rx_us)
      snprintf(line, sizeof(line), "%d;%.1f;%.1f;%.1f;%.1f;%.1f\n", i + 1, (p->visible_us - p->sent_us) / 1000.0,
               (p->rx_us - p->sent_us) / 1000.0, p->host_us / 1000.0, (p->decoded_us - p->rx_us) / 1000.0,
               (p->visible_us - p->decoded_us) / 1000.0);
    else
      snprintf(line, sizeof(line), "%d;%.1f;;%.1f;;%.1f\n", i + 1, (p->visible_us - p->sent_us) / 1000.0,
               p->host_us / 1000.0, (p->visible_us - p->decoded_us) / 1000.0);
    write_str(fd, line);
  }
  sceIoClose(fd);
  LOGI("latency", "result written to %s", path);
}

void latency_test_poll(void) {
  int gen = generation;
  if (gen == written_generation)
    return;
  lt_phase phase = lt_get_phase(&st);
  if (phase != LT_DONE && phase != LT_FAILED)
    return;
  written_generation = gen;
  write_result();
}
