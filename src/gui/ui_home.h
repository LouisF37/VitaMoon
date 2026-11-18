#pragma once

#include <stdbool.h>
#include "../device.h"

/* The home screen: a side menu (Home, Library, Computers, Settings, Help, Quit)
 * and a preview of the entry under the cursor; on Home, a dashboard with the
 * game to pick up. Returns only when the app quits. See home_logic.h. */
void ui_home(void);

/* Pages of ui.c the home opens. */
void ui_computers_page(void);
/* A PC's own menu (connect, wake, change the address, rename, forget). */
void ui_host_manage_menu(device_info_t *info);
/* Sends the Wake-on-LAN packet and says so; false when nothing could be sent. */
bool ui_host_wake(device_info_t *info);
/* HOST_STATE_* of a saved PC (index in known_devices), from the background scan. */
enum { HOST_STATE_OFFLINE, HOST_STATE_ONLINE, HOST_STATE_MOVED };
int ui_host_state(int host_idx);
const char *ui_host_state_text(int state);
