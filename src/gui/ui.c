#include "ui.h"

#include "guilib.h"
#include "splash.h"
#include "theme.h"
#include "ime.h"

#include "ui_settings.h"
#include "ui_connect.h"
#include "ui_device.h"
#include "ui_check.h"
#include "ui_home.h"

#include "../config.h"
#include "../device.h"
#include "../connection.h"
#include "../video/vita.h"
#include "../input/vita.h"
#include "../power/vita.h"
#include "../util.h"
#include "../check_host.h"
#include "../route.h"
#include "../log.h"
#include "../debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include "../i18n.h"

extern SceNetInitParam net_param;

enum {
  MAIN_MENU_CONNECTED = 100,
  MAIN_MENU_SEARCH,
  MAIN_MENU_CONNECT_RESUME,
  MAIN_MENU_SETTINGS,
  MAIN_MENU_CONNECT,
  MAIN_MENU_CONNECT_PAIRED,
  MAIN_MENU_QUIT = 999,
};

// Globals for a pending IP update
int pending_ip_update_idx = -1;
char pending_ip_update[64] = "";
int first_scan_pending = 0;

// Host scan control
#define start_host_scan() start_host_scan_thread()
#define stop_host_scan() stop_host_scan_thread()


enum {
  HOST_MANAGE_INFO = 2000,
  HOST_MANAGE_CONNECT,
  HOST_MANAGE_CHECK_MAC = 3001,
  HOST_MANAGE_WAKE = 3002,
  HOST_MANAGE_DELETE,
  HOST_MANAGE_CHANGE_IP,
  HOST_MANAGE_CHANGE_NAME,
  HOST_MANAGE_BACK
};

/* Index of a device in known_devices, or -1. */
static int host_index(const device_info_t *info) {
  for (int i = 0; i < known_devices.count; i++) {
    if (&known_devices.devices[i] == info)
      return i;
  }
  return -1;
}


/* What the background scan knows about a saved host, for the connection in use. */
int ui_host_state(int host_idx) {
  if (host_idx < 0 || host_idx >= MAX_HOSTS || host_idx >= known_devices.count)
    return HOST_STATE_OFFLINE;
  const device_info_t *info = &known_devices.devices[host_idx];
  struct host_status st = g_host_status[host_idx];
  bool pending = pending_ip_update_idx == host_idx && pending_ip_update[0] != '\0';
  return route_host_state(config.away, info, st.status, st.current_ip, st.on_lan, st.via_internet, pending);
}

const char *ui_host_state_text(int state) {
  switch (state) {
    case HOST_STATE_ONLINE: return config.away ? tr("Online (away)") : tr("Online");
    case HOST_STATE_MOVED: return tr("Address changed");
    case HOST_STATE_AT_HOME: return tr("On the home network");
    case HOST_STATE_NO_ADDRESS: return tr("No Internet address");
    case HOST_STATE_AWAY_ONLY: return tr("Reachable away");
    default: return config.away ? tr("Unreachable") : tr("Offline");
  }
}

const char *ui_host_advice(int state) {
  switch (state) {
    case HOST_STATE_ONLINE: return tr("Ready to play.");
    case HOST_STATE_MOVED: return tr("This PC answers from a new address: open it to update.");
    case HOST_STATE_AT_HOME: return tr("You are at home: set the connection to At home to play here.");
    case HOST_STATE_NO_ADDRESS:
      return tr("Its Internet address is not known yet: connect to it once at home, or type it in its options.");
    case HOST_STATE_AWAY_ONLY:
      return tr("It only answers over the Internet: you are away. Set the connection to Away to play.");
    default:
      return config.away ? tr("No answer over the Internet. Is the PC on, with Sunshine running and its ports open on the box?")
                         : tr("No answer. Is the PC on, with Sunshine running?");
  }
}

const char *ui_host_advice_short(int state) {
  switch (state) {
    case HOST_STATE_AWAY_ONLY: return tr("It only answers over the Internet: you are away from home.");
    case HOST_STATE_NO_ADDRESS: return tr("Its Internet address is not known yet: see its options.");
    case HOST_STATE_OFFLINE:
      if (config.away)
        return tr("No answer over the Internet: is the PC on, its ports open?");
      return ui_host_advice(state);
    default: return ui_host_advice(state);
  }
}

static unsigned int host_state_color(int state) {
  switch (state) {
    case HOST_STATE_ONLINE: return 0xFF00FF00;
    case HOST_STATE_MOVED:
    case HOST_STATE_AT_HOME:
    case HOST_STATE_NO_ADDRESS:
    case HOST_STATE_AWAY_ONLY: return 0xFF00FFFF;
    default: return 0xFF0000FF;
  }
}

void ui_hosts_refresh_internet_address(void) {
  char ip[16];
  if (config.away || !host_scan_public_ip(ip, sizeof(ip)))
    return;
  for (int i = 0; i < known_devices.count && i < MAX_HOSTS; i++) {
    device_info_t *d = &known_devices.devices[i];
    // Only a PC that answers at its home address now is behind this box.
    if (g_host_status[i].status != HOST_ONLINE || !route_is_private_ipv4(d->internal) ||
        strcmp(g_host_status[i].current_ip, d->internal) != 0)
      continue;
    char before[sizeof(d->external)];
    snprintf(before, sizeof(before), "%s", d->external);
    devices_lock();
    bool changed = route_apply_public_ip(d, ip);
    devices_unlock();
    if (changed) {
      save_device_info(d);
      LOGI("route", "%s: Internet address %s (was %s)", d->name, d->external, before[0] ? before : "none");
    }
  }
}

/* Connect to a saved PC, after the questions the scan may raise: a new home
 * address (Home), the PC answering only over the Internet while Home (the
 * Vita is away: Away?), or the PC found on this network while Away (the Vita
 * is at home: back to Home?). */
static void host_connect(device_info_t *info, int app_id) {
  int host_idx = host_index(info);
  char name[sizeof(info->name)];
  snprintf(name, sizeof(name), "%s", info->name);
  if (!config.away && host_idx >= 0 && ui_host_state(host_idx) == HOST_STATE_AWAY_ONLY) {
    // No: nothing else to do, it does not answer at home
    if (!display_confirm(tr("This PC only answers over the Internet:\nyou are away from home.\nSet the connection to Away?")))
      return;
    config.away = true;
    ui_settings_save_config();
    LOGI("route", "connection switched to Away: %s only answers over the Internet", info->name);
  } else if (config.away && host_idx >= 0 && ui_host_state(host_idx) == HOST_STATE_AT_HOME &&
      display_confirm(tr("This PC is on the Vita's network:\nyou are at home.\nSet the connection back to At home?"))) {
    config.away = false;
    ui_settings_save_config();
    LOGI("route", "connection switched to Home: %s is on this network", info->name);
    // The address it was heard at becomes its home address if it changed.
    char seen[64];
    snprintf(seen, sizeof(seen), "%s", g_host_status[host_idx].current_ip);
    if (seen[0] && route_is_ipv4(seen) && strcmp(seen, info->internal) != 0) {
      if (!ui_check_ip_update(info, seen))
        return;
      info = find_device(name);
      if (!info)
        return;
    }
  } else if (!config.away && host_idx >= 0 && pending_ip_update_idx == host_idx && pending_ip_update[0] != '\0') {
    // The scan saw this PC at another home address: offer to use it first
    if (!ui_check_ip_update(info, pending_ip_update))
      return;
    // the list was reloaded: look the PC up again
    info = find_device(name);
    if (!info)
      return;
  }
  ui_hosts_refresh_internet_address();
  if (app_id)
    ui_connect_paired_device_launch(info, app_id);
  else
    ui_connect_paired_device(info);
}

void ui_host_open(int host_idx, int app_id) {
  if (host_idx < 0 || host_idx >= known_devices.count)
    return;
  device_info_t *info = &known_devices.devices[host_idx];
  int state = ui_host_state(host_idx);
  if (state == HOST_STATE_ONLINE || state == HOST_STATE_AT_HOME || state == HOST_STATE_AWAY_ONLY)
    host_connect(info, app_id);
  else
    ui_host_manage_menu(info);
}

/* Sends the Wake-on-LAN packet to a PC and says so on screen. False when
 * nothing could be sent (its network address is not known yet, no IP). */
bool ui_host_wake(device_info_t *info) {
  if (!info->mac[0]) {
    display_notice(3000, "%s", tr("The network address of this PC is not known yet. Connect to it once while it is on."));
    return false;
  }
  // Derive the broadcast address from the internal IP: last number replaced by 255
  char broadcast[32] = {0};
  strncpy(broadcast, info->internal, sizeof(broadcast) - 5);
  char *last_dot = strrchr(broadcast, '.');
  if (!info->internal[0] || !last_dot) {
    display_notice(2000, "%s", tr("Invalid IP"));
    return false;
  }
  strcpy(last_dot + 1, "255");
  extern bool send_wol_packet(const char *mac, const char *ip_broadcast, int port);
  bool wol_ok = send_wol_packet(info->mac, broadcast, 9);
  vita_debug_log("[UI] Wake-on-LAN %s for %s (%s)", wol_ok ? "sent" : "failed", info->name, info->mac);
  display_notice(2500, "%s", wol_ok ? tr("Wake-up signal sent. The PC needs a moment to start.") : tr("Wake-on-LAN failed"));
  return true;
}

static int ui_host_manage_menu_loop(int cursor, void *context, const input_data *input) {
  device_info_t *info = (device_info_t *)context;
  // Allow going back with the cancel button (O)
  if ((input->buttons & config.btn_cancel) != 0) {
    return 1; // Leave the menu
  }
  if ((input->buttons & config.btn_confirm) == 0 || (input->buttons & SCE_CTRL_HOLD) != 0) {
    return 0;
  }
  switch (cursor) {
    case HOST_MANAGE_WAKE:
      vita_debug_log("[UI] Host menu: Wake-on-LAN %s", info->name);
      return ui_host_wake(info) ? 1 : 0;
    case HOST_MANAGE_CONNECT: {
      vita_debug_log("[UI] Host menu: connect to %s", info->name);
      stop_host_scan();
      host_connect(info, 0);
      return 1;
    }
    case HOST_MANAGE_DELETE: {
      char msg[320];
      snprintf(msg, sizeof(msg), tr("Forget %s?\nIts pairing is removed from this Vita."), info->name);
      if (display_confirm(msg)) {
        vita_debug_log("[UI] Host management: delete %s", info->name);
        display_notice(1200, "%s", remove_device(info->name) ? tr("Host deleted") : tr("Error deleting host"));
        return 1;
      }
      return 0;
    }
    case HOST_MANAGE_CHANGE_IP: {
      // The address of the connection in use; the other one is left as it is.
      bool away = config.away;
      vita_debug_log("[UI] Host menu: change the %s address of %s", away ? "Internet" : "home", info->name);
      char new_address[256] = "";
      if (ime_dialog_string(new_address, away ? tr("Internet address of the PC (IP or name):") : tr("Enter new IP:"),
                            route_address(info, away)) != 0)
        return 1;
      // Spaces and a ":port" typed after it are dropped (route_clean_address)
      char cleaned[256];
      route_clean_address(new_address, cleaned, sizeof(cleaned));
      if (away) {
        // Empty is allowed: it forgets the Internet address
        route_set_internet_address(info, cleaned);
        save_device_info(info);
        display_notice(1500, tr("Internet address: %s"), cleaned[0] ? cleaned : tr("(none)"));
      } else if (cleaned[0]) {
        route_set_home_address(info, cleaned);
        save_device_info(info);
        display_notice(1200, tr("IP updated: %s"), cleaned);
      }
      return 1;
    }
    case HOST_MANAGE_CHANGE_NAME: {
      vita_debug_log("[UI] Host menu: rename %s", info->name);
      char new_name[256] = "";
      if (ime_dialog_string(new_name, tr("Enter new name:"), info->name) == 0 && strlen(new_name) > 0 &&
          strcmp(new_name, info->name) != 0) {
        // The host folder (pairing keys included) is renamed: the pairing is kept.
        // The original code wrote a new folder and so lost the pairing.
        if (rename_device(info, new_name))
          display_notice(1200, tr("Name updated: %s"), new_name);
        else
          display_notice(2500, "%s", tr("This name cannot be used (already taken, or it contains / : * ? < > |)."));
      }
      return 1;
    }
  }
  return 0;
}

static int ui_host_manage_menu_back(void *context) {
  return 1;
}

void ui_host_manage_menu(device_info_t *info) {
  menu_entry menu[12];
  int idx = 0;
  int state = ui_host_state(host_index(info));

  // Address of the connection in use and state of the host, then the actions
  char address[320];
  if (config.away && !route_internet_address_usable(info->external))
    snprintf(address, sizeof(address), "%s", tr("No Internet address"));
  else
    snprintf(address, sizeof(address), "%s : %d", route_address(info, config.away), info->port);
  menu[idx] = (menu_entry){ .name = address, .disabled = true, .is_host_entry = true, .color = host_state_color(state) };
  snprintf(menu[idx].subname, sizeof(menu[idx].subname), "%s, %s", ui_host_state_text(state),
           info->paired ? tr("paired") : tr("not paired"));
  idx++;
  // Connect is offered whatever the state shown: the scan can miss a PC that is
  // on, and a PC that does not answer is given up on after a few seconds.
  menu[idx++] = (menu_entry){ .name = tr("Connect"), .id = HOST_MANAGE_CONNECT };
  // Wake-on-LAN only reaches the network the Vita is on: not from away.
  if (!config.away)
    menu[idx++] = (menu_entry){ .name = tr("Wake the PC (Wake-on-LAN)"), .id = HOST_MANAGE_WAKE };
  menu[idx++] = (menu_entry){ .name = config.away ? tr("Change the Internet address") : tr("Change the address"),
                              .id = HOST_MANAGE_CHANGE_IP };
  menu[idx++] = (menu_entry){ .name = tr("Rename"), .id = HOST_MANAGE_CHANGE_NAME };
  menu[idx++] = (menu_entry){ .name = tr("Forget this PC"), .id = HOST_MANAGE_DELETE };

  char title[128];
  snprintf(title, sizeof(title), "%.120s", info->name);
  gui_set_next_title(title);
  display_menu(menu, idx, NULL, &ui_host_manage_menu_loop, &ui_host_manage_menu_back, NULL, info);
}

/* Entry focused on the home screen: kept so the cursor stays put when the
 * list is rebuilt (the host scan refreshes it), and used by the details pane. */
static int home_focus_id = 0;
static bool home_has_focus = false;

static int ui_computers_loop(int cursor, void *context, const input_data *input) {
  // menu_entry *menu = (menu_entry*)context; // unused
  extern volatile int g_host_status_changed;
  extern volatile int g_host_scan_thread_status;
  // Manual refresh with triangle
  if ((input->buttons & SCE_CTRL_TRIANGLE) != 0) {
    vita_debug_log("[UI] Manual refresh (triangle): restarting the host scan");
    stop_host_scan();
    start_host_scan();
    g_host_status_changed = 0; // Clear the flag to avoid a double refresh
    first_scan_pending = 0;
    return 2; // Force a menu refresh
  }
  // Auto refresh once after the first scan, but NOT when an IP changed
  if (first_scan_pending && g_host_status_changed && g_host_scan_thread_status != 1) {
    // When an IP changed, do not force a menu refresh/restart
    int ip_changed = 0;
    for (int i = 0; i < known_devices.count; i++) {
      struct host_status st = g_host_status[i];
      if (st.status == HOST_IP_CHANGED && strcmp(known_devices.devices[i].internal, st.current_ip) != 0) {
        ip_changed = 1;
        break;
      }
    }
    if (!ip_changed) {
      vita_debug_log("[UI] Auto refresh after the first scan\n");
      g_host_status_changed = 0;
      first_scan_pending = 0;
      return 2;
    }
    // When an IP changed, only clear the flags, do NOT restart the menu
    g_host_status_changed = 0;
    first_scan_pending = 0;
  }
  // Refresh on status change only while the scan thread runs
  if (g_host_status_changed && g_host_scan_thread_status == 1) {
    vita_debug_log("[UI] Refreshing the menu after a host status change\n");
    g_host_status_changed = 0;
    return 2; // Force a menu refresh
  }

  ui_hosts_refresh_internet_address();
  home_focus_id = cursor;
  home_has_focus = true;

  bool on_host = cursor >= MAIN_MENU_CONNECT_PAIRED && cursor < MAIN_MENU_QUIT;
  bool held = (input->buttons & SCE_CTRL_HOLD) != 0;
  // Square: the host's own menu (wake, rename, change the address, forget)
  if (on_host && (input->buttons & SCE_CTRL_SQUARE) && !held) {
    stop_host_scan();
    ui_host_manage_menu(&known_devices.devices[cursor - MAIN_MENU_CONNECT_PAIRED]);
    start_host_scan();
    return 2;
  }
  if ((input->buttons & config.btn_confirm) == 0 || held) {
    return 0;
  }
  // X on a host that answers: connect straight away. Anything else (offline,
  // address changed, no Internet address) goes through the host menu.
  if (on_host) {
    stop_host_scan();
    ui_host_open(cursor - MAIN_MENU_CONNECT_PAIRED, 0);
    start_host_scan();
    return 2;
  }

  int exit_menu = 0;
  switch (cursor) {
    case MAIN_MENU_CONNECT:
      vita_debug_log("[UI] Selected: Add manually");
      vita_debug_log("[UI] Stopping the host scan before Add manually");
      stop_host_scan();
      ui_connect_manual();
      vita_debug_log("[UI] Stopping the host scan after Add manually");
      stop_host_scan();
      exit_menu = 2;
      break;
    case MAIN_MENU_SEARCH:
      vita_debug_log("[UI] Selected: Search devices");
      vita_debug_log("[UI] Stopping the host scan before Search devices");
      stop_host_scan();
      ui_search_device();
      vita_debug_log("[UI] Stopping the host scan after Search devices");
      stop_host_scan();
      exit_menu = 2;
      break;
    case MAIN_MENU_CONNECT_RESUME:
      vita_debug_log("[UI] Selected: Resume connection");
      vita_debug_log("[UI] Stopping the host scan before resuming the connection");
      stop_host_scan();
      ui_connect_resume();
      exit_menu = 2;
      break;
  }
  // Leaving the menu: stop the scan
  if (exit_menu) {
    vita_debug_log("[UI] Leaving the main menu, stopping the host scan");
    stop_host_scan();
    return exit_menu;
  }
  return 0;
}


/* ---- Computers page: the list on the left, details of the focused entry on the right ---- */
#define HOME_LIST_W 400
#define HOME_PANE_X (TH_MARGIN + HOME_LIST_W + 32)
#define HOME_PANE_W (TH_RIGHT - HOME_PANE_X)
#define HOME_PANE_Y TH_CONTENT_Y

static int home_wifi_power_save = -1;   /* read when the home screen opens */
static int home_bluetooth = -1;         /* idem */

static void home_detail_row(int y, const char *label, const char *value, unsigned int value_color) {
  theme_text(HOME_PANE_X + 24, y, TH_TEXT3, TS_SMALL, label);
  theme_text_fit_right(HOME_PANE_X + HOME_PANE_W - 24, y, value_color, TS_HINT, value, HOME_PANE_W - 190);
  theme_rect(HOME_PANE_X + 24, y + 12, HOME_PANE_W - 48, 1, TH_DIVIDER);
}

/* Explanation of a non-host entry, wrapped inside the details pane. */
static void home_explain(const char *title, const char *text) {
  theme_text_fit(HOME_PANE_X + 24, HOME_PANE_Y + 44, TH_TEXT, TS_LEAD, title, HOME_PANE_W - 48);
  char lines[6][256];
  int n = theme_wrap(TS_HINT, text, HOME_PANE_W - 48, lines, 6);
  for (int i = 0; i < n; i++)
    theme_text(HOME_PANE_X + 24, HOME_PANE_Y + 80 + i * 24, TH_TEXT2, TS_HINT, lines[i]);
}

static void home_draw(void) {
  int pane_h = TH_CONTENT_END - HOME_PANE_Y;
  theme_glass(HOME_PANE_X, HOME_PANE_Y, HOME_PANE_W, pane_h, 14);

  int id = home_focus_id;
  if (home_has_focus && id >= MAIN_MENU_CONNECT_PAIRED && id < MAIN_MENU_QUIT &&
      id - MAIN_MENU_CONNECT_PAIRED < known_devices.count) {
    int host_idx = id - MAIN_MENU_CONNECT_PAIRED;
    const device_info_t *info = &known_devices.devices[host_idx];
    int state = ui_host_state(host_idx);
    unsigned int state_color = state == HOST_STATE_ONLINE ? TH_OK : (state == HOST_STATE_OFFLINE ? TH_ERR : TH_WARN);

    theme_text_fit(HOME_PANE_X + 24, HOME_PANE_Y + 44, TH_TEXT, TS_LEAD, info->name, HOME_PANE_W - 48);
    theme_circle(HOME_PANE_X + 29, HOME_PANE_Y + 65, 5, state_color);
    theme_text(HOME_PANE_X + 44, HOME_PANE_Y + 70, TH_TEXT2, TS_SMALL, ui_host_state_text(state));

    char value[320];
    int y = HOME_PANE_Y + 112;
    if (config.away && !route_internet_address_usable(info->external))
      snprintf(value, sizeof(value), "%s", tr("Not known yet"));
    else
      snprintf(value, sizeof(value), "%s : %d", route_address(info, config.away), info->port);
    home_detail_row(y, config.away ? tr("Internet address") : tr("Address"), value, TH_TEXT);
    // Only paired PCs are listed: the connection in use says more.
    home_detail_row(y + 38, tr("Connection"), config.away ? tr("Away") : tr("At home"), TH_TEXT);
    home_detail_row(y + 76, tr("Wake-on-LAN"), config.away ? tr("At home only") : (info->mac[0] ? tr("Ready") : tr("Not ready yet")),
                    !config.away && info->mac[0] ? TH_TEXT : TH_TEXT3);
    snprintf(value, sizeof(value), "%d x %d, %d fps", config.stream.width, config.stream.height, config.stream.fps);
    home_detail_row(y + 114, tr("Stream"), value, TH_TEXT);

    /* One piece of advice: what stands between the user and a good stream. */
    bool warn = false;
    const char *advice = ui_host_advice(state);
    if (state == HOST_STATE_ONLINE && home_wifi_power_save == 1) {
      advice = tr("Wi-Fi power save is on and adds lag: turn it off in the Vita's Settings, Power Save Settings.");
      warn = true;
    } else if (state == HOST_STATE_ONLINE && home_bluetooth == 1) {
      // juanjsebgarcia measured frame loss going from about 50 a minute to 0
      // with a strong signal and Bluetooth off.
      advice = tr("Bluetooth is on and can disturb the Wi-Fi: if you use no Bluetooth device, turn it off in the Vita's Settings.");
      warn = true;
    }
    char lines[3][256];
    int n = theme_wrap(TS_SMALL, advice, HOME_PANE_W - 48, lines, 3);
    for (int i = 0; i < n; i++)
      theme_text(HOME_PANE_X + 24, y + 166 + i * 19, warn ? TH_WARN : TH_TEXT2, TS_SMALL, lines[i]);
    return;
  }

  switch (home_has_focus ? id : -1) {
    case MAIN_MENU_CONNECT_RESUME:
      home_explain(tr("Stream paused"), tr("The game is still running on the PC. Resume to go back to it."));
      break;
    case MAIN_MENU_SEARCH:
      home_explain(tr("Search the network"), tr("Lists the PCs running Sunshine on your Wi-Fi network. Pick one, then type the PIN shown on the Vita into Sunshine's web page."));
      break;
    case MAIN_MENU_CONNECT:
      home_explain(tr("Enter an address"), tr("Type the IP address of the PC, as shown in Sunshine or in the network settings of Windows."));
      break;
    default:
      break;
  }
}

/* Hint row of the home screen: what the buttons do on the focused entry. */
static int home_hints(int id, theme_hint_item *out, int max) {
  int n = 0;
  bool on_host = id >= MAIN_MENU_CONNECT_PAIRED && id < MAIN_MENU_QUIT &&
                 id - MAIN_MENU_CONNECT_PAIRED < known_devices.count;
  if (on_host) {
    int state = ui_host_state(id - MAIN_MENU_CONNECT_PAIRED);
    bool online = state == HOST_STATE_ONLINE || state == HOST_STATE_AT_HOME || state == HOST_STATE_AWAY_ONLY;
    out[n++] = (theme_hint_item){theme_confirm_glyph(), online ? tr("Connect") : tr("Open")};
    out[n++] = (theme_hint_item){GLYPH_SQUARE, tr("PC options")};
  } else {
    out[n++] = (theme_hint_item){theme_confirm_glyph(), id == MAIN_MENU_CONNECT_RESUME ? tr("Resume") : tr("Select")};
  }
  if (id != MAIN_MENU_CONNECT_RESUME && n < max)
    out[n++] = (theme_hint_item){GLYPH_TRIANGLE, tr("Refresh")};
  if (n < max)
    out[n++] = (theme_hint_item){theme_cancel_glyph(), tr("Back")};
  return n;
}

static bool rebuilding = false;   /* the page is shown again in place: keep the cursor */

/* One showing of the page; 2 when it must be built again (the scan changed
 * something, a PC was added), 1 when the user leaves it. */
static int ui_computers() {
  // Restart the host scan whenever the page opens
  vita_debug_log("[UI] Entering the Computers page");
  // Scan only when there is NO active connection
  if (!ui_connect_connected()) {
    vita_debug_log("[UI] Starting the host scan (no active connection)");
    start_host_scan();
    first_scan_pending = 1;
  } else {
    vita_debug_log("[UI] Host scan not started: a connection is active");
    first_scan_pending = 0;
    stop_host_scan(); // Stop the scan in case it is still running
  }

  home_wifi_power_save = vitapower_wifi_power_save();
  home_bluetooth = vitapower_bluetooth();

  /* static: an entry is over 1 KB, too much for the stack with many hosts */
  static menu_entry menu[MAX_HOSTS + 12];
  int idx = 0;

#define MENU_ENTRY(ID, NAME, DISABLED) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .disabled = (DISABLED), .id = (ID) }; \
    idx++; \
  } while(0)
#define MENU_SEPARATOR(NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .disabled = true, .separator = true }; \
    idx++; \
  } while(0)

  static char resume_msg[512];
  if (ui_connect_connected()) {
    MENU_SEPARATOR(tr("Current connection"));
    snprintf(resume_msg, sizeof(resume_msg), tr("Resume on %s"), ui_connect_host_label());
    MENU_ENTRY(MAIN_MENU_CONNECT_RESUME, resume_msg, false);
  } else {
    for (int i = 0; i < known_devices.count && i < MAX_HOSTS; i++) {
      device_info_t *cur = &known_devices.devices[i];
      if (!cur->paired) {
        continue;
      }
      // Status from the background scan: green online, red offline, yellow address changed
      int state = ui_host_state(i);
      MENU_ENTRY(MAIN_MENU_CONNECT_PAIRED + i, cur->name, false);
      menu[idx - 1].is_host_entry = true;
      menu[idx - 1].color = host_state_color(state);
      snprintf(menu[idx - 1].subname, sizeof(menu[idx - 1].subname), "%s", ui_host_state_text(state));
    }
    MENU_SEPARATOR(tr("Add a computer"));
    MENU_ENTRY(MAIN_MENU_SEARCH, tr("Search the network"), false);
    MENU_ENTRY(MAIN_MENU_CONNECT, tr("Enter an address"), false);
  }

  menu_geom geom = {0};
  geom.page = true;
  geom.x = TH_MARGIN;
  geom.width = HOME_LIST_W;
  geom.y = TH_CONTENT_Y;
  geom.el = 44;
  geom.height = (TH_CONTENT_END - geom.y) / geom.el * geom.el;
  geom.total_y = geom.y + geom.height;

  /* The list is rebuilt whenever the scan reports a change: keep the cursor
   * where it was and skip the opening animation in that case. */
  if (!rebuilding || !home_has_focus) {
    /* first time: describe the entry the cursor starts on */
    home_has_focus = false;
    for (int i = 0; i < idx; i++) {
      if (!menu[i].disabled) {
        home_focus_id = menu[i].id;
        home_has_focus = true;
        break;
      }
    }
  }
  menu_options options = {0};
  options.title = tr("Computers");
  options.has_focus = home_has_focus;
  options.focus_id = home_focus_id;
  options.instant = rebuilding;
  options.hints_for = home_hints;
  gui_set_next_options(&options);
  return display_menu(menu, idx, &geom, &ui_computers_loop, NULL, &home_draw, NULL);
}

/* The scan is left running for the home screen, which shows the same states. */
void ui_computers_page(void) {
  rebuilding = false;
  home_has_focus = false;
  while (ui_computers() == 2)
    rebuilding = true;
  rebuilding = false;
}

int global_loop(int cursor, void *ctx, const input_data *input) {
  if (is_rectangle_touched(&input->touch, 0, 0, 200, 56)) {
    if (connection_get_status() == LI_MINIMIZED)
      ui_connect_resume_paused();
  }
  return 0;
}

void gui_init() {
  guilib_init(&global_loop, NULL);
}

void gui_loop() {
  gui_init();

  ui_home();

  vita2d_fini();
}
