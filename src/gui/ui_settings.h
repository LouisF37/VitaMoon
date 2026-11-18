#pragma once
int ui_settings_menu();
/* The categories of the Settings page, in its order. */
enum {
  SETTINGS_CAT_GENERAL, SETTINGS_CAT_VIDEO, SETTINGS_CAT_PERFORMANCE, SETTINGS_CAT_CONTROLS, SETTINGS_CAT_TOUCH,
  SETTINGS_CAT_ADVANCED, SETTINGS_CAT_TESTS, SETTINGS_CAT_COUNT
};
/* The Settings page, open on `category` with the cursor on its first row (the
 * home screen's cards); O goes back to the categories, then out. */
void ui_settings_menu_at(int category);
const char *ui_settings_category(int category);
void ui_settings_save_config();
/* Finds the stream sizes the Vita's decoder accepts, in the background (from
 * the start-up); the wait returns once it is done. Settings and the start of a
 * stream wait for it: both use the decoder. */
void ui_settings_probe_start(void);
void ui_settings_probe_wait(void);

/* For the home screen's preview of the settings: the name of a setting and its
 * value, as the Settings page shows them (SETTINGS_SUM_*). */
#include <stddef.h>
enum {
  SETTINGS_SUM_RESOLUTION, SETTINGS_SUM_FPS, SETTINGS_SUM_BITRATE, SETTINGS_SUM_PROFILE, SETTINGS_SUM_LL_AUDIO,
  SETTINGS_SUM_CONTROLLER, SETTINGS_SUM_TOUCH, SETTINGS_SUM_REAR_TOUCH, SETTINGS_SUM_LANGUAGE, SETTINGS_SUM_THEME,
  SETTINGS_SUM_MOVING_SKY, SETTINGS_SUM_REMOTE
};
const char *ui_settings_label(int which);
void ui_settings_value(int which, char *out, size_t n);
/* How many Tests (beta) options differ from 1.0's behaviour; the names of the
 * first `max` go in names. */
int ui_settings_tests_on(const char *names[], int max);
/* How many Advanced options that can be changed now differ from VitaMoon's
 * starting values (config_set_defaults). */
int ui_settings_advanced_changed(void);
