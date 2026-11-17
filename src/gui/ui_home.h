#pragma once

/* The home screen: a side menu (Home, Library, Computers, Settings, Quit) and a
 * preview of the entry under the cursor; on Home, a dashboard with the game to
 * pick up. Returns only when the app quits. See home_logic.h. */
void ui_home(void);

/* Pages of ui.c the home opens. */
void ui_computers_page(void);
/* HOST_STATE_* of a saved PC (index in known_devices), from the background scan. */
enum { HOST_STATE_OFFLINE, HOST_STATE_ONLINE, HOST_STATE_MOVED };
int ui_host_state(int host_idx);
const char *ui_host_state_text(int state);
