#include "ui_connect.h"

#include "Limelight.h"
#include "errors.h"
#include "guilib.h"
#include "ime.h"
#include "ui_settings.h"
#include "boxart.h"
#include "ui_library.h"
#include "../video/stream_stats.h"

#include "../connection.h"
#include "../configuration.h"
#include "../video.h"
#include "../config.h"
#include "../util.h"
#include "../device.h"
#include "../debug.h"

#include "client.h"
#include "http.h"
#include "../platform.h"

#include "../power/vita.h"
#include "../input/vita.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/stat.h>
#include <vita2d.h>
#include "../i18n.h"

SERVER_DATA server;
PAPP_LIST server_applist;
int pos[2];

int get_app_id(PAPP_LIST list, char *name) {
  while (list != NULL) {
    if (strcmp(list->name, name) == 0)
      return list->id;

    list = list->next;
  }
  return -1;
}

int get_app_name(PAPP_LIST list, int id, char *name) {
  while (list != NULL) {
    if (list->id == id) {
      strcpy(name, list->name);
      return 1;
    }

    list = list->next;
  }

  return 0;
}

/* Explains why a PC could not be reached (ret is the gs_init result). */
static void report_host_error(int ret, const char *address) {
  switch (ret) {
    case GS_OUT_OF_MEMORY:
      display_error(tr("Not enough memory"));
      break;
    case GS_INVALID:
      display_error(tr("Unexpected answer from %s:\n%s"), address, gs_error ? gs_error : "");
      break;
    case GS_UNSUPPORTED_VERSION:
      display_error(tr("This Sunshine or GameStream version is not supported:\n%s"), gs_error ? gs_error : "");
      break;
    case GS_ERROR:
      display_error(tr("The PC answered with an error:\n%s"), gs_error ? gs_error : "");
      break;
    default:
      display_error(tr("No answer from %s.\nIs the PC on, with Sunshine running?"), address);
      break;
  }
}

/* Reads the status of a PC (one plain and one secure request) and keeps it in
 * `server`. Returns GS_OK when the PC can be used. */
static int host_open(const char *name, char *address, uint16_t port) {
  char key_dir[4096 + 260];
  snprintf(key_dir, sizeof(key_dir), "%s/%s", config.key_dir, name);
  http_set_reuse(config.http_keepalive);
  int ret = gs_init(&server, address, port, key_dir, config.save_debug_log ? 3 : 0, true);
  if (ret == GS_UNSUPPORTED_VERSION && config.unsupported_version)
    ret = GS_OK;
  vita_debug_log("[CONNECT] %s (%s:%d): status %d", name, address, port, ret);
  return ret;
}

void ui_connect_stream(int appId) {
  // TODO support force controller id
  vita_debug_log("Running gs_start_app...");
  int ret = gs_start_app(&server, &config.stream, appId, config.sops, config.localaudio, 1);
  if (ret < 0) {
    if (ret == GS_NOT_SUPPORTED_4K)
      display_error(tr("The PC cannot stream in 4K."));
    else if (ret == GS_NOT_SUPPORTED_MODE)
      display_error(tr("The PC cannot stream at %d x %d, %d fps.\nChoose another resolution in Settings."),
                    config.stream.width, config.stream.height, config.stream.fps);
    else
      display_error(tr("The PC could not start the game (error %d).\n%s"), ret, gs_error ? gs_error : "");

    return;
  }
  vita_debug_log("App started.");

  enum platform system = VITA;
  int drFlags = 0;

  if (config.fullscreen)
    drFlags |= DISPLAY_FULLSCREEN;

  DECODER_RENDERER_CALLBACKS *video_callback = platform_get_video(system);
  if (config.enable_ref_frame_invalidation) {
    video_callback->capabilities |= CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC;
  } else {
    video_callback->capabilities &= ~CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC;
  }

  // The stream configuration may be adjusted by LiStartConnection: keep the
  // resolution the user asked for and restore it afterwards.
  int orig_width = config.stream.width;
  int orig_height = config.stream.height;

  // Tell the host the Vita's exact refresh rate (e.g. 59.94 instead of 60) so it
  // captures and encodes at the cadence of the screen. Sunshine only accepts it
  // within 1 % of the requested fps, so it is scaled for 30 fps (half rate) and
  // left at 0 (= not sent) for rates that are not a divisor of the screen rate.
  config.stream.clientRefreshRateX100 = 0;
  if (config.match_refresh_rate && config.stream.fps > 0) {
    float screen_hz = 0.0f;
    if (sceDisplayGetRefreshRate(&screen_hz) == 0 && screen_hz > 1.0f) {
      int divisor = (int)(screen_hz / config.stream.fps + 0.5f);
      if (divisor >= 1) {
        float target = screen_hz / divisor;
        float ratio = target / config.stream.fps;
        if (ratio > 0.99f && ratio < 1.01f)
          config.stream.clientRefreshRateX100 = (int)(target * 100.0f + 0.5f);
      }
    }
    vita_debug_log("screen %.4f Hz -> clientRefreshRateX100 = %d\n", screen_hz, config.stream.clientRefreshRateX100);
  }

  stream_stats_set_app(appId);
  vita_debug_log("Running LiStartConnection...");
  ret = LiStartConnection(&server.serverInfo, &config.stream, &connection_callbacks,
                          video_callback, platform_get_audio(system),
                          NULL, drFlags, NULL, 0);
  vita_debug_log("Connection started.");

  // Restore the requested resolution
  config.stream.width = orig_width;
  config.stream.height = orig_height;

  if (ret == 0) {
    server.currentGame = appId;
  } else {
    display_error(tr("The stream could not start.\nFailed step: %s (error %d)"),
                  LiGetStageName(connection_stage), ret);
  }
}

enum {
  CONNECT_PAIR = 13,
  CONNECT_DISCONNECT,
  CONNECT_QUITAPP
};

#define QUIT_RELOAD 2

static char connected_host_name[128] = "";

/* Start (or resume) an app and wait for the stream to end. Returns QUIT_RELOAD
 * to show the library/menu again, 1 if the connection was lost. */
static int ui_launch_app(int id) {
  int status = connection_get_status();
  vitapower_config(config);
  vitainput_config(config);
  // The stream draws from its own thread: no menu frame may be left open.
  ui_end();

  if (status == LI_MINIMIZED && server.currentGame == id) {
    // paused stream of this game: let the button be released, then carry on
    sceKernelDelayThread(500 * 1000);
    connection_resume();
  } else if (status == LI_MINIMIZED || status == LI_PAIRED) {
    flash_message(tr("Starting stream..."));
    ui_connect_stream(id);
  }

  // The stream runs on its own threads: wait here until it is paused or ends.
  while (connection_is_connected()) {
    sceKernelDelayThread(100 * 1000);
  }

  if (connection_get_status() == LI_DISCONNECTED) {
    flash_message(tr("Disconnecting..."));
    connection_terminate();
    sceKernelDelayThread(1000 * 1000);
    return 1;
  }
  return QUIT_RELOAD;
}

/* Quit the app running on the host. */
static int ui_quit_app(void) {
  flash_message(tr("Quitting game..."));
  int ret = gs_quit_app(&server);
  if (ret == GS_OK) {
    connection_paired();
    server.currentGame = 0;
    return QUIT_RELOAD;
  }
  display_error(tr("Could not quit the game (error %d).\n%s"), ret, gs_error ? gs_error : "");
  return 0;
}

/* Shows a four-digit code and waits until it has been typed on the PC (Sunshine
 * holds the request until then). Sunshine's web page listens on the port after
 * the one Moonlight connects to (47990 by default). */
static int pair_with_code(const char *host_name) {
  char pin[5] = {0};
  for (int i = 0; i < 4; i++)
    pin[i] = (char)('0' + (unsigned)rand() % 10u);
  char url[320];
  snprintf(url, sizeof(url), "https://%s:%d", server.serverInfo.address, server.httpPort + 1);
  flash_pairing_code(host_name, pin, url);
  vita_debug_log("[PAIR] waiting for the code to be typed on the PC");
  int ret = gs_pair(&server, pin);
  vita_debug_log("[PAIR] gs_pair returned %d", ret);
  if (ret != GS_OK)
    display_error(tr("Pairing failed (error %d).\n%s\nCheck the code typed on the PC, then try again."),
                  ret, gs_error ? gs_error : "");
  return ret;
}

/* After a successful pairing: remember it, with the PC's MAC for Wake-on-LAN. */
static void remember_pairing(device_info_t *dev) {
  dev->paired = true;
  char mac[18] = {0};
  if (gs_get_server_mac(&server, mac, sizeof(mac)) == GS_OK && mac[0]) {
    snprintf(dev->mac, sizeof(dev->mac), "%s", mac);
    vita_debug_log("[PAIR] MAC saved for %s: %s", dev->name, dev->mac);
  } else {
    vita_debug_log("[PAIR] the PC did not report a MAC");
  }
  save_device_info(dev);
}

int ui_connect_loop(int id, void *context, const input_data *input) {
  int status = connection_get_status();

  if (status == LI_DISCONNECTED) {
      goto disconnect;
  }

  menu_entry *menu = context;
  for (int i = pos[0]; i < pos[1]; i += 1) {
    menu[i].disabled = (server.currentGame != 0);
  }

  if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
    return 0;
  }

  switch (id) {
    case CONNECT_PAIR: {
      // The PC no longer knows this Vita (pairing removed on its side): pair again
      if (pair_with_code(connected_host_name) != GS_OK)
        return 0;
      connection_paired();
      device_info_t *dev = find_device_by_address(server.serverInfo.address);
      if (dev)
        remember_pairing(dev);
      display_notice(1500, tr("%s is paired with this Vita."), connected_host_name);
      return QUIT_RELOAD;
    }

    case CONNECT_DISCONNECT:
      goto disconnect;

    case CONNECT_QUITAPP:
      return ui_quit_app();

    // application launcher / resume
    default:
      return ui_launch_app(id);
  }

disconnect:
  flash_message(tr("Disconnecting..."));
  connection_terminate();
  sceKernelDelayThread(1000 * 1000);
  return 1;
}

int ui_connected_menu() {
  int ret;
  int app_count = 0;
  if (server.paired) {
    ret = gs_applist(&server, &server_applist);
    if (ret != GS_OK) {
      display_error(tr("Could not get the list of games (error %d).\n%s"), ret, gs_error ? gs_error : "");
      return 0;
    }

    if (server_applist != NULL) {
      // Download missing covers in the background for the new library screen.
      boxart_prefetch(&server, server_applist);
      sort_app_list(server_applist);
      PAPP_LIST list = server_applist;
      while (list) {
        list = list->next;
        app_count += 1;
      }
    }
  }

  // New library screen first; the classic menu stays reachable with triangle
  // (pairing, disconnect, plain list of apps).
  static bool show_classic = false;
  if (server.paired && server_applist != NULL && !show_classic) {
    int app_id = 0;
    switch (ui_library(&server, server_applist, connected_host_name, &app_id)) {
      case LIBRARY_LAUNCH:
        return ui_launch_app(app_id);
      case LIBRARY_QUIT_APP:
        ui_quit_app();
        return QUIT_RELOAD;
      case LIBRARY_SETTINGS:
        ui_settings_menu();
        return QUIT_RELOAD;
      case LIBRARY_OPTIONS:
        show_classic = true;
        break;
      case LIBRARY_BACK:
      default:
        return 1;
    }
  }
  bool from_library = show_classic;
  show_classic = false;

  // current menu = 11 + app_count. but little more alloc ;)
  struct menu_entry menu[app_count + 16];

  int idx = 0;

#define MENU_CATEGORY(NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .disabled = true, .separator = true }; \
    idx++; \
  } while (0)
#define MENU_ENTRY(ID, NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .id = (ID) }; \
    idx++; \
  } while(0)
#define MENU_MESSAGE(MESSAGE, COLOR) \
  do { \
    menu[idx] = (menu_entry) { .name = (MESSAGE), .disabled = true, .color = (COLOR) }; \
    idx++; \
  } while(0)

  // One line about the PC, under the page title (its name)
  char server_info[256];
  unsigned short port = server.httpPort ? server.httpPort : server.httpsPort;
  snprintf(server_info, sizeof(server_info), "%s : %d", server.serverInfo.address, port);
  MENU_MESSAGE(server_info, 0xffffffff);
  snprintf(menu[idx - 1].subname, sizeof(menu[idx - 1].subname), "%s", server.gpuType ? server.gpuType : "");

  // Filled even when a branch below returns early, so the loop never reads stale positions
  pos[0] = pos[1] = 0;
  char current_status[256];

  if (!server.paired) {
    // pairing
    MENU_CATEGORY(tr("Not paired"));
    MENU_ENTRY(CONNECT_PAIR, tr("Pair"));

    MENU_ENTRY(CONNECT_DISCONNECT, tr("Disconnect"));
  } else {
    // current stream
    if (server.currentGame != 0) {
      char current_appname[256];

      if (!get_app_name(server_applist, server.currentGame, current_appname)) {
        strcpy(current_appname, tr("unknown"));
      }
      snprintf(current_status, sizeof(current_status), tr("Streaming %s"), current_appname);

      MENU_CATEGORY(current_status);
      MENU_ENTRY(server.currentGame, tr("Resume"));
      MENU_ENTRY(CONNECT_QUITAPP, tr("Quit"));
    }

    // pairing
    MENU_CATEGORY(tr("Paired"));
    connection_paired();

    MENU_ENTRY(CONNECT_DISCONNECT, tr("Disconnect"));

    // app list
    if (server_applist != NULL) {
      MENU_CATEGORY(tr("Applications"));

      pos[0] = idx;

      PAPP_LIST list = server_applist;
      while (list) {
        MENU_ENTRY(list->id, list->name);
        list = list->next;
      }

      pos[1] = idx;
    }
  }

  gui_set_next_title(connected_host_name[0] ? connected_host_name : tr("Computer"));
  int menu_ret = display_menu(menu, idx, NULL, &ui_connect_loop, NULL, NULL, &menu);
  // Back from the classic menu opened from the library: return to the library.
  if (from_library && menu_ret == 1 && connection_get_status() != LI_DISCONNECTED)
    return QUIT_RELOAD;
  return menu_ret;
}

/* Runs the connected screens (library, classic menu) until the user leaves.
 * A session without a running or paused stream is then forgotten, so the home
 * screen lists the PCs again instead of only offering to resume. */
static void connected_screens(void) {
  while (ui_connected_menu() == QUIT_RELOAD);
  connection_close_idle();
}

/* Adds a PC (found by the search or typed by hand) and pairs with it.
 * Returns the saved entry, or NULL. */
device_info_t* ui_connect_and_pairing(device_info_t *info) {
  if (!info->port)
    info->port = 47989;
  flash_message(tr("Connecting to %s..."), info->internal);
  vita_debug_log("[CONNECT] adding name=%s, ip=%s, port=%d", info->name, info->internal, info->port);

  int ret = host_open(info->name, info->internal, info->port);
  if (ret != GS_OK) {
    report_host_error(ret, info->internal);
    return NULL;
  }

  // Save the PC now (its folder also holds the pairing keys), then work on the
  // saved entry: `info` may be a temporary of the caller.
  device_info_t *saved = append_device(info);
  if (saved == NULL) {
    if (!update_device(info)) {
      display_error(tr("Could not save this PC."));
      return NULL;
    }
    saved = find_device(info->name);
    if (saved == NULL)
      return NULL;
  }
  info = saved;
  server.serverInfo.address = info->internal;
  save_device_info(info);

  if (!server.paired && pair_with_code(info->name) != GS_OK)
    return NULL;

  remember_pairing(info);
  display_notice(1500, tr("%s is paired with this Vita."), info->name);
  return info;
}

void ui_connect_resume() {
  connected_screens();
}

void ui_connect_manual() {
  device_info_t info = {0};
  if (ime_dialog_string(info.internal, tr("Address of the PC (for example 192.168.1.20)"), "") != 0 || !info.internal[0]) {
    return;
  }
  // Suggest a name that is not taken: an existing name means "same PC, new address"
  char suggestion[32] = "PC";
  for (int n = 2; find_device(suggestion) && n < 100; n++)
    snprintf(suggestion, sizeof(suggestion), "PC %d", n);
  if (ime_dialog_string(info.name, tr("Name for this PC"), suggestion) != 0) {
    return;
  }
  // The name is also a folder name: fall back on the suggestion
  if (!info.name[0] || strpbrk(info.name, "/\\:*?\"<>|"))
    snprintf(info.name, sizeof(info.name), "%s", suggestion);
  info.port = 47989;
  ui_connect_and_pairing(&info);
}

void ui_connect_paired_device(device_info_t *info) {
  // A stream is running or paused: go back to it
  if (connection_is_ready()) {
    connected_screens();
    return;
  }
  if (!info->paired) {
    display_error(tr("%s is not paired with this Vita.\nAdd it again from the home screen."), info->name);
    return;
  }

  flash_message(tr("Connecting to %s..."), info->name);

  // Home address first, unless the Internet address worked last time. One status
  // request per address; the first PC that answers is used as it is (the
  // original code asked twice: once to test the address, once to connect).
  char *order[2] = { info->internal, info->external };
  if (info->prefer_external) {
    order[0] = info->external;
    order[1] = info->internal;
  }
  char *addr = NULL;
  int ret = GS_FAILED;
  for (int i = 0; i < 2 && addr == NULL; i++) {
    if (!order[i][0] || (i == 1 && strcmp(order[0], order[1]) == 0))
      continue;
    ret = host_open(info->name, order[i], info->port);
    if (ret == GS_OK)
      addr = order[i];
  }

  if (addr == NULL) {
    report_host_error(ret, info->name);
    return;
  }

  bool external = addr == info->external && strcmp(info->internal, info->external) != 0;
  if (external != info->prefer_external) {
    info->prefer_external = external;
    save_device_info(info);
  }

  snprintf(connected_host_name, sizeof(connected_host_name), "%.127s", info->name);
  connection_reset();
  connected_screens();
}

bool ui_connect_connected() {
  return connection_is_ready();
}

void ui_connect_address(char *addr) {
  strcpy(addr, server.serverInfo.address);
}

/* Name of the PC of the current session (its address if the name is unknown). */
const char *ui_connect_host_label(void) {
  return connected_host_name[0] ? connected_host_name : server.serverInfo.address;
}
