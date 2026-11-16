#include "ui_settings.h"

#include "guilib.h"
#include "theme.h"
#include "ime.h"

#include "../config.h"
#include "../device.h"
#include "../device_name.h"
#include "../connection.h"
//#include "../debug.h"
#include "../input/vita.h"
#include "ui_connect.h"
#include "debug.h"
#include "udp_sniffer_vita.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <Limelight.h>
#include <client.h>
#include <errors.h>
#include "../i18n.h"

#define MILLISECOND 1000
#define SECOND      (1000 * MILLISECOND)

enum {
  DEVICE_ITEM = 101,   /* menu ids of the PCs found: DEVICE_ITEM + index */
};

enum {
  SEARCH_THREAD_IDLE,
  SEARCH_THREAD_RUNNING,
  SEARCH_THREAD_REQ_STOP,
};

int search_thread_status = SEARCH_THREAD_IDLE;

static device_info_t devices[64];
static int found_device = 0;

void ipv4_address_to_string(const struct sockaddr_in *addr, char *ip, const size_t len) {
  inet_ntop(AF_INET, &addr->sin_addr.s_addr, ip, len);
}

// part of publib. BSD license
// https://github.com/ajkaijanaho/publib/blob/master/strutil/strrstr.c
char* strrstr(const char *str, const char *pat) {
  size_t len, patlen;
  const char *p;

  assert(str != NULL);
  assert(pat != NULL);

  len = strlen(str);
  patlen = strlen(pat);

  if (patlen > len)
    return NULL;
  for (p = str + (len - patlen); p > str; --p)
    if (*p == *pat && strncmp(p, pat, patlen) == 0)
      return (char *) p;
  return NULL;
}

static void moonlight_found_callback(int idx, const char* host, const char* pcname, const char* ip, int port) {
    vita_debug_log("[mDNS] Device found: %s (%s:%d)\n", pcname, ip, port);
    if (found_device >= (int)(sizeof(devices) / sizeof(devices[0]))) {
        vita_debug_log("[mDNS] List full, %s ignored\n", pcname);
        return;
    }
    // The name becomes a folder name: keep it a plain one.
    char name[sizeof(devices[0].name)];
    strncpy(name, pcname, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    device_name_sanitize(name, sizeof(name), "PC");
    // Skip a device with the same name and IP
    for (int i = 0; i < found_device; i++) {
        if (strncmp(devices[i].name, name, sizeof(devices[i].name)) == 0 &&
            strncmp(devices[i].internal, ip, sizeof(devices[i].internal)) == 0) {
            vita_debug_log("[mDNS] Duplicate device ignored: %s (%s)\n", name, ip);
            return;
        }
    }
    // Same name but a different IP is added
    memset(&devices[found_device], 0, sizeof(device_info_t));
    snprintf(devices[found_device].name, sizeof(devices[found_device].name), "%s", name);
    strncpy(devices[found_device].internal, ip, sizeof(devices[found_device].internal)-1);
    devices[found_device].port = port;
    found_device++;
}

int mdns_discovery_main(SceSize args, void *argp) {
  vita_debug_log("[mDNS] Searching for devices...\n");
  if (search_thread_status != SEARCH_THREAD_IDLE) {
    vita_debug_log("[mDNS] search_thread_status != IDLE, saliendo.\n");
    return 0;
  }

  search_thread_status = SEARCH_THREAD_RUNNING;
  found_device = 0;

  udp_sniffer_vita_deinit(); // close the socket and reset the state
  udp_sniffer_vita_init();   // reset the state (optional, for symmetry)
  udp_sniffer_vita_set_callback(moonlight_found_callback);

  // Wait and process for 10 seconds at most
  int ticks = 0;
  int max_ticks = 100; // 100 * 100ms = 10s
  while (search_thread_status == SEARCH_THREAD_RUNNING && ticks < max_ticks) {
      udp_sniffer_vita_poll();
      sceKernelDelayThread(1000 * 100); // 100ms
      ticks++;
  }

  search_thread_status = SEARCH_THREAD_IDLE;
  vita_debug_log("[mDNS] Search done. found_device=%d\n", found_device);
  return 0;
}

static SceUID search_thread_id = -1;

int end_search_thread(SceUID thid);

void stop_search_thread_if_running() {
  vita_debug_log("[mDNS] stop_search_thread_if_running: status=%d, thid=%d\n", search_thread_status, search_thread_id);
  if (search_thread_status == SEARCH_THREAD_RUNNING && search_thread_id > 0) {
    vita_debug_log("[mDNS] Calling end_search_thread(%d)\n", search_thread_id);
    end_search_thread(search_thread_id);
    vita_debug_log("[mDNS] end_search_thread done\n");
    search_thread_id = -1;
  }
}

static void clear_devices() {
  vita_debug_log("[mDNS] Clearing the device list\n");
  memset(devices, 0, sizeof(devices));
  found_device = 0;
}

SceUID start_search_thread() {
  vita_debug_log("[mDNS] start_search_thread: clearing and creating the thread\n");
  clear_devices();
  SceUID thid = sceKernelCreateThread("mdns", mdns_discovery_main, 0x10000100, 0x10000, 0, 0, NULL);
  if (thid < 0) {
    vita_debug_log("[mDNS] Could not create the search thread\n");
    return -1;
  }
  sceKernelStartThread(thid, 0, 0);
  search_thread_id = thid;
  vita_debug_log("[mDNS] Search thread started: thid=%d\n", thid);
  return thid;
}

// Forward declaration

int end_search_thread(SceUID thid) {
  vita_debug_log("[mDNS] end_search_thread: stopping thread %d\n", thid);
  search_thread_status = SEARCH_THREAD_REQ_STOP;

  SceUInt timeout;
  do {
    timeout = 100 * MILLISECOND;
  } while (sceKernelWaitThreadEnd(thid, NULL, &timeout) < 0);

  sceKernelDeleteThread(thid);
  vita_debug_log("[mDNS] Thread %d deleted\n", thid);
  return 0;
}

static int listed_devices = 0;      /* found_device when the list was last built */
static int search_focus_id = 0;
static bool search_reopened = false;

static int ui_search_device_callback(int id, void *context, const input_data *input) {
  if ((input->buttons & SCE_CTRL_TRIANGLE) != 0) {
    vita_debug_log("[mDNS] TRIANGLE pressed: refreshing the search\n");
    stop_search_thread_if_running();
    start_search_thread();
    return 2; // Force a menu refresh
  }
  search_focus_id = id;
  if ((input->buttons & config.btn_confirm) == 0 || (input->buttons & SCE_CTRL_HOLD) != 0) {
    // a PC was found since the list was built: rebuild it
    if (found_device != listed_devices) {
      return 2;
    }
    return 0;
  }
  if (id >= DEVICE_ITEM) {

    // Address, port (and MAC when known) come from the mDNS answer
    device_info_t *dev = &devices[id - DEVICE_ITEM];
    device_info_t *info = ui_connect_and_pairing(dev);
    if (info == NULL) {
      return 0;
    }

    unsigned int extern_addr = 0;
    if (LiFindExternalAddressIP4("stun.stunprotocol.org", 3478, &extern_addr) == 0) {
      struct sockaddr_in addr;
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = extern_addr;
      ipv4_address_to_string(&addr, info->external, 16);
    }

    save_device_info(info);

    return 1;
  }
  //TODO: Return proper on all code paths
  return 0;
}

static int ui_search_device_back(void *context) {
  return 0;
}

int ui_search_device_loop() {
  int idx = 0;
  static menu_entry menu[64];
  listed_devices = found_device;

  menu[idx++] = (menu_entry) { .name = tr("PCs found on the network"), .disabled = true, .separator = true };
  int shown = 0;
  for (int i = 0; i < found_device && idx < 60; i++) {
    if (devices[i].internal[0] == '\0') {
      continue;
    }
    device_info_t *p = find_device(devices[i].name);
    if (p && p->paired) {
      continue;
    }
    // name on the left, address on the right (kept in the entry itself)
    menu[idx] = (menu_entry) { .name = devices[i].name, .id = DEVICE_ITEM + i };
    snprintf(menu[idx].subname, sizeof(menu[idx].subname), "%s", devices[i].internal);
    idx++;
    shown++;
  }
  if (!shown) {
    menu[idx] = (menu_entry) { .name = "", .disabled = true };
    snprintf(menu[idx].subname, sizeof(menu[idx].subname), "%s",
             tr("Searching... Sunshine must be running on the PC, on the same network."));
    idx++;
  }

  theme_hint_item hints[3] = {
    {theme_confirm_glyph(), tr("Pair")},
    {GLYPH_TRIANGLE, tr("Search again")},
    {theme_cancel_glyph(), tr("Back")},
  };
  menu_options options = {0};
  options.title = tr("Search the network");
  options.has_focus = search_reopened;
  options.focus_id = search_focus_id;
  options.instant = search_reopened;   /* rebuilt in place when a PC appears */
  options.hints = hints;
  options.hint_count = 3;
  gui_set_next_options(&options);
  search_reopened = true;
  return display_menu(menu, idx, NULL, &ui_search_device_callback, &ui_search_device_back, NULL, &menu);
}

void ui_search_device() {
  stop_search_thread_if_running(); // Always stop any previous search
  start_search_thread(); // Always search when entering
  search_reopened = false;

  while (ui_search_device_loop() == 2);

  stop_search_thread_if_running(); // Stop the search when leaving
}
