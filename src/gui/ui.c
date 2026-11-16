#include "ui.h"

#include "guilib.h"
#include "theme.h"
#include "ime.h"

#include "ui_settings.h"
#include "ui_connect.h"
#include "ui_device.h"
#include "ui_check.h"

#include "../config.h"
#include "../device.h"
#include "../connection.h"
#include "../video/vita.h"
#include "../input/vita.h"
#include "../power/vita.h"
#include "../util.h"
#include "../check_host.h"
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

enum { HOST_STATE_OFFLINE, HOST_STATE_ONLINE, HOST_STATE_MOVED };

/* What the background scan knows about a saved host. */
static int host_state(int host_idx) {
  if (host_idx < 0 || host_idx >= MAX_HOSTS)
    return HOST_STATE_OFFLINE;
  const device_info_t *info = &known_devices.devices[host_idx];
  struct host_status st = g_host_status[host_idx];
  if (pending_ip_update_idx == host_idx && pending_ip_update[0] != '\0')
    return HOST_STATE_MOVED;
  if (st.current_ip[0] && strcmp(info->internal, st.current_ip) != 0)
    return HOST_STATE_MOVED;
  return st.status == HOST_ONLINE ? HOST_STATE_ONLINE : HOST_STATE_OFFLINE;
}

static const char *host_state_text(int state) {
  switch (state) {
    case HOST_STATE_ONLINE: return tr("Online");
    case HOST_STATE_MOVED: return tr("Address changed");
    default: return tr("Offline");
  }
}

static unsigned int host_state_color(int state) {
  switch (state) {
    case HOST_STATE_ONLINE: return 0xFF00FF00;
    case HOST_STATE_MOVED: return 0xFF00FFFF;
    default: return 0xFF0000FF;
  }
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
    case HOST_MANAGE_WAKE: {
      vita_debug_log("[UI] Host menu: Wake-on-LAN %s", info->name);
      if (!info->mac[0]) {
        display_notice(3000, "%s", tr("The network address of this PC is not known yet. Connect to it once while it is on."));
        return 0;
      }
      // Derive the broadcast address from the internal IP: last number replaced by 255
      char broadcast[32] = {0};
      strncpy(broadcast, info->internal, sizeof(broadcast) - 5);
      char *last_dot = strrchr(broadcast, '.');
      if (!info->internal[0] || !last_dot) {
        display_notice(2000, "%s", tr("Invalid IP"));
        return 0;
      }
      strcpy(last_dot + 1, "255");
      extern bool send_wol_packet(const char *mac, const char *ip_broadcast, int port);
      bool wol_ok = send_wol_packet(info->mac, broadcast, 9);
      vita_debug_log("[UI] Wake-on-LAN %s for %s (%s)", wol_ok ? "sent" : "failed", info->name, info->mac);
      display_notice(2500, "%s", wol_ok ? tr("Wake-up signal sent. The PC needs a moment to start.") : tr("Wake-on-LAN failed"));
      return 1;
    }
    case HOST_MANAGE_CONNECT: {
      vita_debug_log("[UI] Host menu: connect to %s", info->name);
      stop_host_scan();
      // The scan saw this PC at another address: offer to use it first
      int host_idx = host_index(info);
      if (host_idx >= 0 && pending_ip_update_idx == host_idx && pending_ip_update[0] != '\0') {
        char name[sizeof(info->name)];
        snprintf(name, sizeof(name), "%s", info->name);
        if (!ui_check_ip_update(info, pending_ip_update))
          return 1;
        // the list was reloaded: look the PC up again
        info = find_device(name);
        if (!info)
          return 1;
      }
      ui_connect_paired_device(info);
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
      vita_debug_log("[UI] Host menu: change IP %s", info->name);
      char new_ip[256] = "";
      if (ime_dialog_string(new_ip, tr("Enter new IP:"), info->internal) == 0 && strlen(new_ip) > 0) {
        strncpy(info->internal, new_ip, sizeof(info->internal)-1);
        info->internal[sizeof(info->internal)-1] = '\0';
        strncpy(info->external, new_ip, sizeof(info->external)-1);
        info->external[sizeof(info->external)-1] = '\0';
        info->prefer_external = false;
        save_device_info(info);
        display_notice(1200, tr("IP updated: %s"), new_ip);
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
  int state = host_state(host_index(info));

  // Address and state of the host, then the actions
  char address[320];
  snprintf(address, sizeof(address), "%s : %d", info->internal, info->port);
  menu[idx] = (menu_entry){ .name = address, .disabled = true, .is_host_entry = true, .color = host_state_color(state) };
  snprintf(menu[idx].subname, sizeof(menu[idx].subname), "%s, %s", host_state_text(state),
           info->paired ? tr("paired") : tr("not paired"));
  idx++;
  // Connect is offered whatever the state shown: the scan can miss a PC that is
  // on, and a PC that does not answer is given up on after a few seconds.
  menu[idx++] = (menu_entry){ .name = tr("Connect"), .id = HOST_MANAGE_CONNECT };
  menu[idx++] = (menu_entry){ .name = tr("Wake the PC (Wake-on-LAN)"), .id = HOST_MANAGE_WAKE };
  menu[idx++] = (menu_entry){ .name = tr("Change the address"), .id = HOST_MANAGE_CHANGE_IP };
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

int ui_main_menu_loop(int cursor, void *context, const input_data *input) {
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
  // address changed) goes through the host menu, which handles those cases.
  if (on_host) {
    int host_idx = cursor - MAIN_MENU_CONNECT_PAIRED;
    device_info_t *info = &known_devices.devices[host_idx];
    stop_host_scan();
    if (host_state(host_idx) == HOST_STATE_ONLINE)
      ui_connect_paired_device(info);
    else
      ui_host_manage_menu(info);
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
    case MAIN_MENU_SETTINGS:
      vita_debug_log("[UI] Selected: Settings");
      vita_debug_log("[UI] Stopping the host scan before Settings");
      stop_host_scan();
      {
        int lang_before = i18n_language();
        ui_settings_menu();
        if (i18n_language() != lang_before) {
          exit_menu = 2; // rebuild the menu so its labels use the new language
          break;
        }
      }
      exit_menu = 0;
      start_host_scan();
      break;
    case MAIN_MENU_QUIT:
      vita_debug_log("[UI] Selected: Quit");
      vita_debug_log("[UI] Stopping the host scan before quitting");
      stop_host_scan();
      if (connection_get_status() != LI_DISCONNECTED) {
        connection_terminate();
      }
      exit(0);
      return 0;
  }
  // Leaving the menu: stop the scan
  if (exit_menu) {
    vita_debug_log("[UI] Leaving the main menu, stopping the host scan");
    stop_host_scan();
    return exit_menu;
  }
  return 0;
}

int ui_main_menu_back(void *context) {
  return 1;
}
/* ---- home screen: the list on the left, details of the focused entry on the right ---- */
#define HOME_LIST_W 400
#define HOME_PANE_X (TH_MARGIN + HOME_LIST_W + 32)
#define HOME_PANE_W (TH_RIGHT - HOME_PANE_X)
#define HOME_PANE_Y (TH_CONTENT_Y0 + 8)

static char home_version[96];
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
  theme_text(TH_MARGIN, TH_HINTS_Y, TH_TEXT3, TS_MICRO, home_version);

  int id = home_focus_id;
  if (home_has_focus && id >= MAIN_MENU_CONNECT_PAIRED && id < MAIN_MENU_QUIT &&
      id - MAIN_MENU_CONNECT_PAIRED < known_devices.count) {
    int host_idx = id - MAIN_MENU_CONNECT_PAIRED;
    const device_info_t *info = &known_devices.devices[host_idx];
    int state = host_state(host_idx);
    unsigned int state_color = state == HOST_STATE_ONLINE ? TH_OK : (state == HOST_STATE_MOVED ? TH_WARN : TH_ERR);

    theme_text_fit(HOME_PANE_X + 24, HOME_PANE_Y + 44, TH_TEXT, TS_LEAD, info->name, HOME_PANE_W - 48);
    theme_circle(HOME_PANE_X + 29, HOME_PANE_Y + 65, 5, state_color);
    theme_text(HOME_PANE_X + 44, HOME_PANE_Y + 70, TH_TEXT2, TS_SMALL, host_state_text(state));

    char value[320];
    int y = HOME_PANE_Y + 112;
    snprintf(value, sizeof(value), "%s : %d", info->internal, info->port);
    home_detail_row(y, tr("Address"), value, TH_TEXT);
    home_detail_row(y + 38, tr("Pairing"), info->paired ? tr("Paired") : tr("Not paired"), TH_TEXT);
    home_detail_row(y + 76, tr("Wake-on-LAN"), info->mac[0] ? tr("Ready") : tr("Not ready yet"),
                    info->mac[0] ? TH_TEXT : TH_TEXT3);
    snprintf(value, sizeof(value), "%d x %d, %d fps", config.stream.width, config.stream.height, config.stream.fps);
    home_detail_row(y + 114, tr("Stream"), value, TH_TEXT);

    /* One piece of advice: what stands between the user and a good stream. */
    bool warn = false;
    const char *advice;
    if (state == HOST_STATE_MOVED) {
      advice = tr("This PC answers from a new address: open it to update.");
    } else if (state != HOST_STATE_ONLINE) {
      advice = tr("No answer. Is the PC on, with Sunshine running?");
    } else if (home_wifi_power_save == 1) {
      advice = tr("Wi-Fi power save is on and adds lag: turn it off in the Vita's Settings, Power Save Settings.");
      warn = true;
    } else if (home_bluetooth == 1) {
      // juanjsebgarcia measured frame loss going from about 50 a minute to 0
      // with a strong signal and Bluetooth off.
      advice = tr("Bluetooth is on and can disturb the Wi-Fi: if you use no Bluetooth device, turn it off in the Vita's Settings.");
      warn = true;
    } else {
      advice = tr("Ready to play.");
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
    case MAIN_MENU_SETTINGS:
      home_explain(tr("Settings"), tr("Language, video quality, performance profile, controls and touch screen."));
      break;
    case MAIN_MENU_QUIT:
      home_explain(tr("Quit"), tr("Closes VitaMoon and returns to the Vita home screen."));
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
    bool online = host_state(id - MAIN_MENU_CONNECT_PAIRED) == HOST_STATE_ONLINE;
    out[n++] = (theme_hint_item){theme_confirm_glyph(), online ? tr("Connect") : tr("Open")};
    out[n++] = (theme_hint_item){GLYPH_SQUARE, tr("PC options")};
  } else {
    out[n++] = (theme_hint_item){theme_confirm_glyph(), id == MAIN_MENU_CONNECT_RESUME ? tr("Resume") : tr("Select")};
  }
  if (id != MAIN_MENU_CONNECT_RESUME && n < max)
    out[n++] = (theme_hint_item){GLYPH_TRIANGLE, tr("Refresh")};
  return n;
}

int ui_main_menu() {
  // Restart the host scan whenever the main menu opens
  vita_debug_log("[UI] Entering the main menu");
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

#ifdef BUILD_ID
  snprintf(home_version, sizeof(home_version), "%s  %s", VERSION_TEXT, BUILD_ID);
#else
  snprintf(home_version, sizeof(home_version), "%s", VERSION_TEXT);
#endif

  static char resume_msg[512];
  if (ui_connect_connected()) {
    MENU_SEPARATOR(tr("Current connection"));
    snprintf(resume_msg, sizeof(resume_msg), tr("Resume on %s"), ui_connect_host_label());
    MENU_ENTRY(MAIN_MENU_CONNECT_RESUME, resume_msg, false);
  } else {
    bool any_host = false;
    for (int i = 0; i < known_devices.count && i < MAX_HOSTS; i++) {
      device_info_t *cur = &known_devices.devices[i];
      if (!cur->paired) {
        continue;
      }
      if (!any_host) {
        MENU_SEPARATOR(tr("Computers"));
        any_host = true;
      }
      // Status from the background scan: green online, red offline, yellow address changed
      int state = host_state(i);
      MENU_ENTRY(MAIN_MENU_CONNECT_PAIRED + i, cur->name, false);
      menu[idx - 1].is_host_entry = true;
      menu[idx - 1].color = host_state_color(state);
      snprintf(menu[idx - 1].subname, sizeof(menu[idx - 1].subname), "%s", host_state_text(state));
    }
    MENU_SEPARATOR(tr("Add a computer"));
    MENU_ENTRY(MAIN_MENU_SEARCH, tr("Search the network"), false);
    MENU_ENTRY(MAIN_MENU_CONNECT, tr("Enter an address"), false);
  }

  MENU_ENTRY(MAIN_MENU_SETTINGS, tr("Settings"), false);
  MENU_ENTRY(MAIN_MENU_QUIT, tr("Quit"), false);

  menu_geom geom = {0};
  geom.page = true;
  geom.x = TH_MARGIN;
  geom.width = HOME_LIST_W;
  geom.y = TH_CONTENT_Y0;
  geom.el = 44;
  geom.height = (TH_CONTENT_END - geom.y) / geom.el * geom.el;
  geom.total_y = geom.y + geom.height;

  /* The list is rebuilt whenever the scan reports a change: keep the cursor
   * where it was and skip the opening animation in that case. */
  static bool opened_before = false;
  if (!home_has_focus) {
    /* first time: describe the entry the cursor starts on */
    for (int i = 0; i < idx; i++) {
      if (!menu[i].disabled) {
        home_focus_id = menu[i].id;
        home_has_focus = true;
        break;
      }
    }
  }
  menu_options options = {0};
  options.has_focus = home_has_focus;
  options.focus_id = home_focus_id;
  options.instant = opened_before;
  options.hints_for = home_hints;
  gui_set_next_options(&options);
  opened_before = true;
  return display_menu(menu, idx, &geom, &ui_main_menu_loop, &ui_main_menu_back, &home_draw, NULL);
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

  while (ui_main_menu() == 2);

  vita2d_fini();
}
