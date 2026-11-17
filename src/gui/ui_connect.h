#pragma once
#include <stdbool.h>
#include "../device.h"

void ui_connect_address(char *addr);
const char *ui_connect_host_label(void);
device_info_t* ui_connect_and_pairing(device_info_t *info);
bool ui_connect_connected();

void ui_connect_resume();
/* Resumes the paused stream (offering a restart if settings that apply to a
 * new stream changed) and waits until it is paused again or ends. */
int ui_connect_resume_paused(void);
void ui_connect_manual();
void ui_connect_paired_device(device_info_t *info);
/* Opens the PC and starts (or resumes) app_id at once, without the library
 * first; the library follows when the stream is paused or ends. */
void ui_connect_paired_device_launch(device_info_t *info, int app_id);
/* The same for the paused stream: back into its game. */
void ui_connect_resume_launch(void);
/* The game of the session in progress (a paused stream): 0 / "" when none. */
int ui_connect_current_game_id(void);
const char *ui_connect_current_game_name(void);
#ifdef UI_PREVIEW
/* Draws the waiting moon of a stream start at `stage` (0: before the PC is
 * asked for the game), for the UI preview. */
void ui_connect_preview_moon(const char *host, int stage);
/* Names the PC of the session, as opening it does. */
void ui_connect_preview_host(const char *name);
#endif
