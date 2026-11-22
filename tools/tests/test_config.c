/*
 * Settings file (src/config.c): every setting written by config_save() is read
 * back by config_file_parse(), and values from older or edited files are
 * checked or ignored as intended.
 */
#include "test.h"
#include "config.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

/* What config.c needs from the rest of the app. */
void vita_debug_log(const char *s, ...) { (void)s; }
static int errors_logged = 0;
void log_write(log_level level, const char *tag, const char *fmt, ...) {
  (void)tag; (void)fmt;
  if (level == LOG_ERROR)
    errors_logged++;
}
int sceKernelGetModelForCDialog(void) { return 0; }
void LiInitializeStreamConfiguration(PSTREAM_CONFIGURATION c) { memset(c, 0, sizeof(*c)); }

static const char *k_file = "/tmp/vitamoon-test.conf";

static void write_text(const char *text) {
  FILE *f = fopen(k_file, "w");
  fputs(text, f);
  fclose(f);
}

static CONFIGURATION sample(void) {
  CONFIGURATION c;
  memset(&c, 0, sizeof(c));
  c.stream.width = 1280;
  c.stream.height = 720;
  c.stream.fps = 30;
  c.stream.bitrate = 7000;
  c.auto_bitrate = false;
  c.away = true;              /* not the default: the file must carry it */
  c.away_bitrate = 4500;
  c.away_auto_bitrate = false;
  c.stream.packetSize = 1200;
  c.sops = false;
  c.localaudio = true;
  c.enable_frame_pacer = true;
  c.center_region_only = true;
  c.disable_powersave = true;
  c.jp_layout = true;
  c.show_fps = true;
  c.save_debug_log = true;
  c.enable_front_touchzones = true;
  c.mouse_acceleration = 175;
  c.library_stats = true;
  c.twinkle_stars = false;
  c.sky_speed = 3;
  c.sky_anticlockwise = true;
  c.tap_time_ms = 150;
  c.rear_touch = false;       /* not the default: the file must carry it */
  c.test_decoder_delay = 3;
  c.test_decoder_refs = 2;
  c.test_skip_late = true;
  c.test_immediate_flip = true;
  c.test_audio_5ms = true;
  c.enable_ref_frame_invalidation = true;
  c.stream.streamingRemotely = 1;
  c.enable_vita_vblank_wait = true;
  c.boost_clocks = true;
  c.match_refresh_rate = true;
  c.low_latency_audio = true;
  c.display_mode = DISPLAY_MODE_PACED;
  c.decoder_catchup = true;
  c.http_keepalive = true;
  c.ui_msaa = true;
  c.decode_yuv = true;
  c.video_reorder_wait_ms = 30;
  c.thread_pinning = true;
  c.input_sync = true;
  c.auto_reconnect = false;   /* not the default: the file must carry it */
  c.enable_motion_controls = true;
  c.enable_psbutton_capture = true;
  c.enable_double_tap_sprint = true;
  c.double_tap_sprint_step_time = 250;
  c.motion_controls_scalar_x = 1.5f;
  c.motion_controls_scalar_y = 0.75f;
  c.keyboard_layout = 1;
  c.language = 2;
  c.theme = 3;
  c.touchscreen_mode = 3;
  c.swap_shoulder_buttons = true;
  c.controller_type = 1;
  c.back_deadzone.top = 11;
  c.back_deadzone.right = 22;
  c.back_deadzone.bottom = 33;
  c.back_deadzone.left = 44;
  c.special_keys.nw = 0x1234;
  c.special_keys.ne = 0x10;
  c.special_keys.sw = 0xABC;
  c.special_keys.se = 0x7;
  c.special_keys.offset = 5;
  c.special_keys.size = 160;
  snprintf(c.key_dir, sizeof(c.key_dir), "ux0:data/vitamoon/");
  return c;
}

int main(void) {
  TEST_BEGIN("config");

  CONFIGURATION a = sample();
  config_save(k_file, &a);
  CONFIGURATION b;
  memset(&b, 0, sizeof(b));
  config_file_parse((char *)k_file, &b);

#define SAME(field) CHECK(a.field == b.field, "round trip: " #field)
  SAME(rear_touch); SAME(test_decoder_delay); SAME(test_decoder_refs); SAME(test_skip_late);
  SAME(test_immediate_flip); SAME(test_audio_5ms);
  SAME(library_stats); SAME(twinkle_stars); SAME(sky_speed); SAME(sky_anticlockwise); SAME(tap_time_ms);
  SAME(stream.width); SAME(stream.height); SAME(stream.fps); SAME(stream.bitrate);
  SAME(auto_bitrate); SAME(away); SAME(away_bitrate); SAME(away_auto_bitrate);
  SAME(stream.packetSize); SAME(sops); SAME(localaudio);
  SAME(enable_frame_pacer); SAME(center_region_only); SAME(disable_powersave); SAME(jp_layout);
  SAME(show_fps); SAME(save_debug_log); SAME(enable_front_touchzones); SAME(mouse_acceleration);
  SAME(enable_ref_frame_invalidation); SAME(stream.streamingRemotely); SAME(enable_vita_vblank_wait);
  SAME(boost_clocks); SAME(match_refresh_rate); SAME(low_latency_audio); SAME(display_mode);
  SAME(decoder_catchup); SAME(http_keepalive); SAME(ui_msaa); SAME(decode_yuv);
  SAME(video_reorder_wait_ms); SAME(thread_pinning); SAME(input_sync); SAME(auto_reconnect); SAME(enable_motion_controls);
  SAME(enable_psbutton_capture); SAME(enable_double_tap_sprint); SAME(double_tap_sprint_step_time);
  SAME(keyboard_layout); SAME(language); SAME(theme); SAME(touchscreen_mode); SAME(swap_shoulder_buttons);
  SAME(controller_type); SAME(back_deadzone.top); SAME(back_deadzone.right); SAME(back_deadzone.bottom);
  SAME(back_deadzone.left); SAME(special_keys.nw); SAME(special_keys.ne); SAME(special_keys.sw);
  SAME(special_keys.se); SAME(special_keys.offset); SAME(special_keys.size);
#undef SAME
  CHECK(b.motion_controls_scalar_x > 1.49f && b.motion_controls_scalar_x < 1.51f, "round trip: motion scalar x");
  CHECK(b.motion_controls_scalar_y > 0.74f && b.motion_controls_scalar_y < 0.76f, "round trip: motion scalar y");

  /* The old "packetsize" key was written but never read: its value never took
   * effect, so it must not override the default now. */
  CONFIGURATION c;
  memset(&c, 0, sizeof(c));
  c.stream.packetSize = PACKET_SIZE_DEFAULT;
  write_text("packetsize = 1392\n");
  config_file_parse((char *)k_file, &c);
  CHECK(c.stream.packetSize == PACKET_SIZE_DEFAULT, "old packetsize key ignored");

  write_text("packet_size = 9000\nvideo_reorder_wait_ms = 99\n");
  config_file_parse((char *)k_file, &c);
  CHECK(c.stream.packetSize == PACKET_SIZE_DEFAULT, "out-of-range packet size falls back to the default");
  CHECK(c.video_reorder_wait_ms == 50, "reorder wait clamped to 50 ms");

  write_text("video_reorder_wait_ms = -5\n");
  config_file_parse((char *)k_file, &c);
  CHECK(c.video_reorder_wait_ms == 0, "negative reorder wait clamped to 0");

  /* A bitrate saved by a version without auto_bitrate was a manual choice. */
  memset(&c, 0, sizeof(c));
  c.auto_bitrate = true;
  write_text("bitrate = 5000\n");
  config_file_parse((char *)k_file, &c);
  CHECK(c.stream.bitrate == 5000 && !c.auto_bitrate, "old bitrate line read as a manual bitrate");

  /* The Away bitrate is read the same way: a value with no away_auto_bitrate
   * line after it was chosen by hand; "connection" other than "away" is Home. */
  memset(&c, 0, sizeof(c));
  c.away_auto_bitrate = true;
  write_text("away_bitrate = 3000\nconnection = away\n");
  config_file_parse((char *)k_file, &c);
  CHECK(c.away_bitrate == 3000 && !c.away_auto_bitrate && c.away, "away bitrate and connection read");
  write_text("connection = somewhere\n");
  config_file_parse((char *)k_file, &c);
  CHECK(!c.away, "unknown connection value: Home");
  memset(&c, 0, sizeof(c));
  write_text("away_bitrate = 900000\n");
  config_file_parse((char *)k_file, &c);
  config_sanitize(&c);
  CHECK(c.away_bitrate == -1 && c.away_auto_bitrate, "sanitize: absurd away bitrate back to automatic");

  /* Out-of-range values from a file go back to their defaults. */
  memset(&c, 0, sizeof(c));
  write_text("width = 5000\nheight = 10\nfps = 240\nbitrate = 900000\ndisplay_mode = 9\n"
             "touchscreen_mode = -2\ncontroller_type = 0\nmouse_acceleration = 4000\n"
             "motion_controls_scalar_x = nan\nmotion_controls_scalar_y = 99\n"
             "double_tap_sprint_step_time = 5\nlanguage = 7\npacket_size = 1300\ntheme = 9\n"
             "keyboard_layout = 12\ntap_time_ms = 30\nsky_speed = 9\n"
             "test_decoder_delay = 5\ntest_decoder_refs = 3\n");
  config_file_parse((char *)k_file, &c);
  config_sanitize(&c);
  CHECK(c.stream.width == 960 && c.stream.height == 544, "sanitize: resolution back to 960x544");
  CHECK(c.stream.fps == 60, "sanitize: fps back to 60");
  CHECK(c.stream.bitrate == -1 && c.auto_bitrate, "sanitize: absurd bitrate back to automatic");
  CHECK(c.display_mode == DISPLAY_MODE_SMOOTH, "sanitize: unknown display mode back to Smooth");
  CHECK(c.touchscreen_mode == 0 && c.controller_type == 2, "sanitize: touch mode and controller type");
  CHECK(c.mouse_acceleration == 150, "sanitize: mouse acceleration");
  CHECK(c.tap_time_ms == TAP_TIME_DEFAULT, "sanitize: tap time out of 100..250 back to 250");
  CHECK(c.sky_speed == SKY_STILL, "sanitize: unknown sky speed back to a still sky");
  CHECK(c.test_decoder_delay == TEST_DELAY_OFF, "sanitize: unknown decoder delay mode back to off");
  CHECK(c.test_decoder_refs == TEST_REFS_DEFAULT, "sanitize: 3 reference frames back to 4");
  CHECK(c.motion_controls_scalar_x > 1.19f && c.motion_controls_scalar_x < 1.21f, "sanitize: NaN motion scale");
  CHECK(c.motion_controls_scalar_y > 0.79f && c.motion_controls_scalar_y < 0.81f, "sanitize: huge motion scale");
  CHECK(c.double_tap_sprint_step_time == 200, "sanitize: sprint tap time");
  CHECK(c.language == 0, "sanitize: unknown language back to English");
  CHECK(c.theme == 0, "sanitize: unknown theme back to Night");
  CHECK(c.keyboard_layout == CONFIG_KEYBOARD_TEXT, "sanitize: unknown keyboard layout back to text");
  CHECK(c.stream.packetSize == 1300, "sanitize: a valid packet size is kept");
  /* Pointer acceleration 0 is "off" in the settings: it survives a restart. */
  memset(&c, 0, sizeof(c));
  write_text("mouse_acceleration = 0\ntap_time_ms = 100\nsky_speed = 4\n"
             "test_decoder_delay = 4\ntest_decoder_refs = 1\n");
  config_file_parse((char *)k_file, &c);
  config_sanitize(&c);
  CHECK(c.mouse_acceleration == 0, "sanitize: acceleration 0 (off) is kept");
  CHECK(c.tap_time_ms == 100, "sanitize: a tap time of 100 ms is kept");
  CHECK(c.sky_speed == SKY_SPEED_COUNT - 1, "sanitize: the fastest sky is kept");
  CHECK(c.test_decoder_delay == TEST_DELAY_COUNT - 1, "sanitize: the last decoder delay mode is kept");
  CHECK(c.test_decoder_refs == 1, "sanitize: one reference frame is kept");
  CONFIGURATION good = sample();
  CONFIGURATION copy = good;
  config_sanitize(&copy);
  CHECK(memcmp(&good, &copy, sizeof(good)) == 0, "sanitize leaves valid settings alone");

  /* Automatic bitrate: within the range the Vita's Wi-Fi handles. */
  int b544 = config_auto_bitrate(960, 544, 60);
  int b720 = config_auto_bitrate(1280, 720, 60);
  int b720_30 = config_auto_bitrate(1280, 720, 30);
  printf("    auto bitrate: 960x544@60 %d, 1280x720@60 %d, 1280x720@30 %d kbps\n", b544, b720, b720_30);
  CHECK(b544 > 0 && b544 <= b720, "auto bitrate grows with the resolution");
  CHECK(b720_30 <= b720, "auto bitrate lower at 30 fps");
  CHECK(b720 <= 20000, "auto bitrate stays within 20 Mbps");

  /* Away: three quarters of the Home bitrate, both recomputed together; a
   * stream starts with the bitrate of the connection in use. */
  CHECK(config_away_auto_bitrate(960, 544, 60) == 6000, "away auto bitrate: 6 Mbps at 960x544, 60 fps");
  CHECK(config_away_auto_bitrate(1280, 720, 60) == 7500, "away auto bitrate: 7.5 Mbps at 1280x720, 60 fps");
  memset(&c, 0, sizeof(c));
  c.stream.width = 960; c.stream.height = 544; c.stream.fps = 60;
  c.auto_bitrate = true; c.away_auto_bitrate = true;
  config_apply_auto_bitrate(&c);
  CHECK(c.stream.bitrate == 8000 && c.away_bitrate == 6000, "auto bitrates applied: Home 8000, Away 6000");
  c.away_auto_bitrate = false; c.away_bitrate = 12000;
  c.stream.fps = 30;
  config_apply_auto_bitrate(&c);
  CHECK(c.stream.bitrate == 5000 && c.away_bitrate == 12000, "a manual Away bitrate is left alone");
  CHECK(config_stream_bitrate(&c) == 5000, "Home connection: the stream uses the Home bitrate");
  c.away = true;
  CHECK(config_stream_bitrate(&c) == 12000, "Away connection: the stream uses the Away bitrate");

  /* A settings file that cannot be written is logged, and the app goes on. */
  config_save("/nonexistent-folder/vitamoon.conf", &a);
  CHECK(errors_logged == 1, "settings file that cannot be written: logged, app keeps running");

  /* The look VitaMoon starts with (1.1.0-beta.3): Eclipse, the stars turning
   * around the moon (Medium: one turn in 8 min, see test_library). */
  static CONFIGURATION d;
  memset(&d, 0, sizeof(d));
  config_set_defaults(&d);
  CHECK(d.theme == CONFIG_THEME_DEFAULT && CONFIG_THEME_DEFAULT == 4, "default theme: Eclipse (theme 4)");
  CHECK(d.sky_speed == SKY_MEDIUM && d.twinkle_stars && !d.sky_anticlockwise,
        "default sky: moving (Medium), twinkling, clockwise");
  CHECK(d.stream.width == 960 && d.stream.height == 544 && d.stream.fps == 60 && d.sops && d.thread_pinning,
        "config_set_defaults sets the stream and the rest as config_parse did");
  CHECK(!d.away && d.away_auto_bitrate && d.stream.streamingRemotely == 0,
        "default connection: Home, automatic Away bitrate, not optimised for the Internet");

  remove(k_file);
  return TEST_END();
}
