int ui_settings_menu();
void ui_settings_save_config();
/* Finds the stream sizes the Vita's decoder accepts, in the background (from
 * the start-up); the wait returns once it is done. Settings and the start of a
 * stream wait for it: both use the decoder. */
void ui_settings_probe_start(void);
void ui_settings_probe_wait(void);
