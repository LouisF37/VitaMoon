#pragma once

#include <stdbool.h>
#include "../device.h"
#include "../route.h"

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
/* HOST_STATE_* (route.h) of a saved PC (index in known_devices), from the
 * background scan and the Connection setting. */
int ui_host_state(int host_idx);
const char *ui_host_state_text(int state);
/* One sentence on what stands between the user and a stream on this PC;
 * the short form fits the one line of the home screen's large card. */
const char *ui_host_advice(int state);
const char *ui_host_advice_short(int state);
/* X on a saved PC (index in known_devices): connect when it answers (straight
 * into app_id when not 0), offer to switch back to Home when the Away scan
 * hears it at home, its menu otherwise. */
void ui_host_open(int host_idx, int app_id);
/* Gives the box's public address found by the scan (check_host.h) to the PCs
 * that answer at home, as their Internet address when it may be replaced
 * (route_apply_public_ip); saves those that changed. */
void ui_hosts_refresh_internet_address(void);
