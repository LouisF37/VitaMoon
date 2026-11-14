#include "ui.h"

#include "guilib.h"
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
  HOST_MANAGE_FORCE_CONNECT,
  HOST_MANAGE_BACK
};

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
    case HOST_MANAGE_CHECK_MAC: {
      vita_debug_log("[UI] Host menu: check MAC %s", info->name);
      char mac[18] = {0};
      if (info->mac[0]) {
        strncpy(mac, info->mac, sizeof(mac)-1);
        mac[sizeof(mac)-1] = '\0';
        char msg[64];
        snprintf(msg, sizeof(msg), "MAC: %s", mac);
        flash_message(msg);
        vita_debug_log("[UI] MAC saved for %s: %s", info->internal, mac);
      } else {
        flash_message(tr("No MAC saved for this host"));
        vita_debug_log("[UI] No MAC saved for %s", info->internal);
      }
      sceKernelDelayThread(2*1000*1000); // Wait 2 seconds so the message can be read
      return 1;
    }
    case HOST_MANAGE_WAKE: {
      vita_debug_log("[UI] Host menu: Wake-on-LAN %s", info->name);
      if (!info->mac[0]) {
        flash_message(tr("No MAC saved for this host"));
        vita_debug_log("[UI] Wake-on-LAN: No MAC for %s", info->name);
        sceKernelDelayThread(2*1000*1000);
        return 1;
      }
      // Derive the broadcast address from the internal IP
      char broadcast[32] = {0};
      const char *ip = info->internal;
      if (!ip[0]) {
        flash_message(tr("No IP for this host"));
        return 1;
      }
      // Copy the IP and replace the last octet with 255
      strncpy(broadcast, ip, sizeof(broadcast)-1);
      char *last_dot = strrchr(broadcast, '.');
      if (last_dot) {
        strcpy(last_dot+1, "255");
      } else {
        flash_message(tr("Invalid IP"));
        return 1;
      }
      extern bool send_wol_packet(const char *mac, const char *ip_broadcast, int port);
      bool wol_ok = send_wol_packet(info->mac, broadcast, 9);
      if (wol_ok) {
        flash_message(tr("Wake-on-LAN sent!"));
        vita_debug_log("[UI] Wake-on-LAN sent to %s (%s)", info->name, info->mac);
      } else {
        flash_message(tr("Wake-on-LAN failed"));
        vita_debug_log("[UI] Wake-on-LAN failed for %s (%s)", info->name, info->mac);
      }
      sceKernelDelayThread(2*1000*1000);
      return 1;
    }
    case HOST_MANAGE_CONNECT: {
      vita_debug_log("[UI] Host menu: connect to %s", info->name);
      stop_host_scan();
      // If the IP changed, ask before pairing
      extern int pending_ip_update_idx;
      extern char pending_ip_update[64];
      int host_idx = -1;
      for (int i = 0; i < known_devices.count; i++) {
        if (&known_devices.devices[i] == info) {
          host_idx = i;
          break;
        }
      }
      if (host_idx >= 0 && pending_ip_update_idx == host_idx && pending_ip_update[0] != '\0') {
        char msg[320];
        snprintf(msg, sizeof(msg), tr("Host IP changed!\nOld: %s\nNew: %s\nDo you want to pair with the new IP?"), info->internal, pending_ip_update);
        int res = display_confirm(msg);
        if (res) {
          ui_check_ip_update(info, pending_ip_update);
          device_info_t *updated = find_device(info->name);
          if (updated) info = updated;
          ui_connect_paired_device(info);
        } else {
          flash_message(tr("Connection cancelled"));
        }
      } else {
        ui_connect_paired_device(info);
      }
      return 1;
    }
    case HOST_MANAGE_DELETE: {
      char msg[320];
      snprintf(msg, sizeof(msg), tr("Delete host?\nAre you sure you want to delete %s?"), info->name);
      int res = display_confirm(msg);
      if (res) {
        vita_debug_log("[UI] Host management: delete %s", info->name);
        if (remove_device(info->name)) {
          flash_message(tr("Host deleted"));
        } else {
          flash_message(tr("Error deleting host"));
        }
        return 1;
      } else {
        flash_message(tr("Delete cancelled"));
        return 0;
      }
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
        flash_message(tr("IP updated: %s"), new_ip);
      } else {
        flash_message(tr("IP change cancelled"));
      }
      return 1;
    }
    case HOST_MANAGE_CHANGE_NAME: {
      vita_debug_log("[UI] Host menu: rename %s", info->name);
      char new_name[256] = "";
      if (ime_dialog_string(new_name, tr("Enter new name:"), info->name) == 0 && strlen(new_name) > 0) {
        // Keep the old name to delete the old file
        char old_name[256];
        strncpy(old_name, info->name, sizeof(old_name)-1);
        old_name[sizeof(old_name)-1] = '\0';
        strncpy(info->name, new_name, sizeof(info->name)-1);
        info->name[sizeof(info->name)-1] = '\0';
        save_device_info(info);
        // Delete the old file if the name changed
        if (strcmp(old_name, new_name) != 0) {
          remove_device(old_name);
        }
        flash_message(tr("Name updated: %s"), new_name);
      } else {
        flash_message(tr("Name change cancelled"));
      }
      return 1;
    }
    case HOST_MANAGE_FORCE_CONNECT:
      vita_debug_log("[UI] Host menu: forced connection to %s", info->name);
      stop_host_scan();
      ui_connect_paired_device(info);
      return 1;
    case HOST_MANAGE_BACK:
      return 1;
  }
  return 0;
}

static int ui_host_manage_menu_back(void *context) {
  return 1;
}

void ui_host_manage_menu(device_info_t *info) {
  menu_entry menu[12];
  int idx = 0;
  char title[256];
  snprintf(title, sizeof(title), tr("Host: %s"), info->name);
  // Header rows
  menu[idx++] = (menu_entry){ .name = tr("Host management"), .disabled = true, .color = 0xFFFFFFFF };
  menu[idx++] = (menu_entry){ .name = title, .disabled = true, .color = 0xFF00AAFF };
  // IP and status with a colored dot
  char ipinfo[320];
  char status_text[64];
  snprintf(status_text, sizeof(status_text), "%s", tr("Unpaired"));
  int host_idx = -1;
  for (int i = 0; i < known_devices.count; i++) {
    if (&known_devices.devices[i] == info) {
      host_idx = i;
      break;
    }
  }
  if (host_idx >= 0) {
    struct host_status st = g_host_status[host_idx];
    if (pending_ip_update_idx == host_idx && pending_ip_update[0] != '\0') {
      snprintf(status_text, sizeof(status_text), "%s", tr("[IP changed]"));
    } else if (st.current_ip[0] && strcmp(info->internal, st.current_ip) != 0) {
      snprintf(status_text, sizeof(status_text), "%s", tr("[IP changed]"));
    } else {
      if (st.status == HOST_ONLINE) {
        snprintf(status_text, sizeof(status_text), tr("%s (Online)"), info->paired ? tr("Paired") : tr("Unpaired"));
      } else {
        snprintf(status_text, sizeof(status_text), tr("%s (Offline)"), info->paired ? tr("Paired") : tr("Unpaired"));
      }
    }
  }
  // IP and port on one line
  snprintf(ipinfo, sizeof(ipinfo), "IP: %s  PORT: %d", info->internal, info->port);
  menu[idx] = (menu_entry){ .name = ipinfo, .disabled = true };
  strncpy(menu[idx].subname, status_text, sizeof(menu[idx].subname)-1);
  menu[idx].subname[sizeof(menu[idx].subname)-1] = '\0';
  idx++;
  menu[idx++] = (menu_entry){ .name = "", .disabled = true, .separator = true };
  // Options (no Info entry)
  menu[idx++] = (menu_entry){ .name = tr("Connect"), .id = HOST_MANAGE_CONNECT };
  // menu[idx++] = (menu_entry){ .name = "Check MAC", .id = HOST_MANAGE_CHECK_MAC }; // Check MAC hidden
  menu[idx++] = (menu_entry){ .name = tr("Wake on LAN (WOL)"), .id = HOST_MANAGE_WAKE };
// Extra option for testing
#define HOST_MANAGE_CHECK_MAC 3001
  menu[idx++] = (menu_entry){ .name = tr("Force connect"), .id = HOST_MANAGE_FORCE_CONNECT };
  menu[idx++] = (menu_entry){ .name = tr("Delete"), .id = HOST_MANAGE_DELETE };
  menu[idx++] = (menu_entry){ .name = tr("Change IP"), .id = HOST_MANAGE_CHANGE_IP };
  menu[idx++] = (menu_entry){ .name = tr("Change Name"), .id = HOST_MANAGE_CHANGE_NAME };
  menu[idx++] = (menu_entry){ .name = tr("Back"), .id = HOST_MANAGE_BACK };
  menu_geom geom = make_geom_centered(600, 320);
  display_menu(menu, idx, &geom, &ui_host_manage_menu_loop, &ui_host_manage_menu_back, NULL, info);
}

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

  if ((input->buttons & config.btn_confirm) == 0 || (input->buttons & SCE_CTRL_HOLD) != 0) {
    return 0;
  }
  // The host menu is always reachable, even offline or after an IP change
  if (cursor >= MAIN_MENU_CONNECT_PAIRED && cursor < MAIN_MENU_QUIT) {
    int host_idx = cursor - MAIN_MENU_CONNECT_PAIRED;
    device_info_t *info = &known_devices.devices[host_idx];
    stop_host_scan();
    ui_host_manage_menu(info);
    start_host_scan();
    return 2;
  }

  // Any option that leaves this menu stops the host scan
  int exit_menu = 0;
  if (cursor >= MAIN_MENU_CONNECT_PAIRED && cursor < MAIN_MENU_QUIT) {
    device_info_t *info = &known_devices.devices[cursor - MAIN_MENU_CONNECT_PAIRED];
    int host_idx = cursor - MAIN_MENU_CONNECT_PAIRED;
    struct host_status st;
    st = g_host_status[host_idx];
    vita_debug_log("[UI] (LOOP) host_idx=%d, scan_state=%d, st.status=%d, st.current_ip=%s", host_idx, g_host_scan_thread_status, st.status, st.current_ip);

    if (st.status == HOST_IP_CHANGED && strcmp(info->internal, st.current_ip) != 0) {
      vita_debug_log("[UI] Host %s detected IP change: old=%s, new=%s. Deferring IP update dialog.", info->name, info->internal, st.current_ip);
      pending_ip_update_idx = host_idx;
      strncpy(pending_ip_update, st.current_ip, sizeof(pending_ip_update)-1);
      pending_ip_update[sizeof(pending_ip_update)-1] = '\0';
      exit_menu = 1;
    } else if (st.status == HOST_ONLINE) {
      vita_debug_log("[UI] Host %s is ONLINE. Connecting normally.", info->name);
      vita_debug_log("[UI] Stopping the host scan before connecting");
      stop_host_scan();
      ui_connect_paired_device(info);
      exit_menu = 2;
    } else {
      vita_debug_log("[UI] Host %s is OFFLINE or unreachable.", info->name);
      flash_message(tr("Host is offline or unreachable."));
      return 0;
    }
  }
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

  menu_entry menu[16];
  int idx = 0;

#define MENU_TITLE(NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = "", .disabled = true, .color = 0xff00aa00 }; \
    strcpy(menu[idx].subname, (NAME)); \
    idx++; \
  } while (0)
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

  char program_info[256];
#ifdef __vita__
#ifdef BUILD_ID
  snprintf(program_info, 256, "Moonlight v%d.%d.%d (%s)", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, BUILD_ID);
#else
  snprintf(program_info, 256, "Moonlight v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
#endif
  MENU_TITLE(program_info);
#endif

  //char name[256] = {0};
  char addr[256] = {0};
  char resume_msg[512] = {0};
  //if (ui_connect_connected()) {
  //  ui_connect_address(addr);
  //  sprintf(name, "Resume connection to %s", addr);
  //} else {
  //  sprintf(name, "Connect to %s", config.address ? config.address : "none");
  //}

  static char last_ip_logged[256] = "";
  if (ui_connect_connected()) {
    MENU_SEPARATOR(tr("Current connection"));
    ui_connect_address(addr);
    sprintf(resume_msg, tr("Resume connection to %s"), addr);
    MENU_ENTRY(MAIN_MENU_CONNECT_RESUME, resume_msg, false);
  } else {
    MENU_SEPARATOR(tr("Add new computer"));
    MENU_ENTRY(MAIN_MENU_SEARCH, tr("Search devices ..."), false);
    MENU_ENTRY(MAIN_MENU_CONNECT, tr("Add manually ..."), false);

    if (known_devices.count) {
      MENU_SEPARATOR(tr("Paired computers"));
      // Use the scan thread results
      for (int i = 0; i < known_devices.count; i++) {
        device_info_t *cur = &known_devices.devices[i];
        if (!cur->paired) {
          continue;
        }
        struct host_status st;
        st = g_host_status[i];

        // unsigned int color = 0; // No usado
        // const char* color_str = ""; // No usado
        // bool ip_changed = (st.current_ip[0] && strcmp(cur->internal, st.current_ip) != 0); // No usado
        // bool ip_pending = (pending_ip_update_idx == i && pending_ip_update[0] != '\0'); // No usado
        MENU_ENTRY(MAIN_MENU_CONNECT_PAIRED + i, cur->name, false);
        int real_idx = idx - 1;
        menu[real_idx].is_host_entry = true;
        // Pending IP change for this host: force yellow and skip the rest
        if (pending_ip_update_idx == i && pending_ip_update[0] != '\0') {
          strcpy(menu[real_idx].subname, tr("[IP changed]"));
          menu[real_idx].color = 0xFF00FFFF;
          vita_debug_log("[UI] Host %s menu idx=%d, color=0x%08X (YELLOW, IP pending)", cur->name, real_idx, 0xFF00FFFF);
          continue;
        }
        // ...normal color logic...
        if (st.current_ip[0] && strcmp(cur->internal, st.current_ip) != 0) {
          if (strcmp(last_ip_logged, st.current_ip) != 0) {
            vita_debug_log("[UI] Host %s IP changed: old=%s, new=%s", cur->name, cur->internal, st.current_ip);
            strncpy(last_ip_logged, st.current_ip, sizeof(last_ip_logged)-1);
            last_ip_logged[sizeof(last_ip_logged)-1] = '\0';
          }
          strcpy(menu[real_idx].subname, tr("[IP changed]"));
          menu[real_idx].color = 0xFF00FFFF;
          vita_debug_log("[UI] Host %s menu idx=%d, color=0x%08X (YELLOW)", cur->name, real_idx, 0xFF00FFFF);
        } else if (strcmp(menu[real_idx].subname, tr("[IP changed]")) == 0) {
          menu[real_idx].color = 0xFF00FFFF;
          vita_debug_log("[UI] Host %s menu idx=%d, color=0x%08X (YELLOW)", cur->name, real_idx, 0xFF00FFFF);
        } else if (st.status == HOST_ONLINE) {
          menu[real_idx].color = 0xFF00FF00;
          vita_debug_log("[UI] Host %s menu idx=%d, color=0x%08X (GREEN)", cur->name, real_idx, 0xFF00FF00);
        } else {
          menu[real_idx].color = 0xFF0000FF;
          vita_debug_log("[UI] Host %s menu idx=%d, color=0x%08X (RED)", cur->name, real_idx, 0xFF0000FF);
        }
      }
    }
  }

  MENU_SEPARATOR("");
  MENU_ENTRY(MAIN_MENU_SETTINGS, tr("Settings"), false);
  MENU_ENTRY(MAIN_MENU_QUIT, tr("Quit"), false);

  // Start the scan only if no IP change is pending and it has not run yet
  menu_geom geom = make_geom_centered(560, 330);
  return display_menu(menu, idx, &geom, &ui_main_menu_loop, &ui_main_menu_back, NULL, NULL);
}

int global_loop(int cursor, void *ctx, const input_data *input) {
  if (is_rectangle_touched(&input->touch, 0, 0, 150, 150)) {
    if (connection_get_status() == LI_MINIMIZED) {
      vitapower_config(config);
      vitainput_config(config);

      sceKernelDelayThread(500 * 1000);
      connection_resume();

      while (connection_is_connected()) {
        sceKernelDelayThread(500 * 1000);
      }
    }
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
