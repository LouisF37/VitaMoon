/*
 * UI preview: renders the real menu code (src/gui) to PNG files on a PC.
 *
 *   preview <scenario> <language: en|fr|es> <output folder>
 *
 * A scenario presses buttons and takes snapshots through the script helpers
 * of preview_sys.c, then calls the real screen function, which runs until the
 * script ends. Everything outside src/gui is replaced by the stand-ins below.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <psp2/ctrl.h>
#include <Limelight.h>

#include "preview.h"
#include "config.h"
#include "device.h"
#include "check_host.h"
#include "connection.h"
#include "client.h"
#include "errors.h"
#include "i18n.h"
#include "platform.h"
#include "gui/guilib.h"
#include "gui/theme.h"
int ui_main_menu(void);
#include "gui/ui_settings.h"
#include "gui/ui_library.h"
#include "gui/boxart.h"
#include "video/stream_stats.h"

/* ---------------- stand-ins for the rest of the app ---------------- */

static device_info_t fake_devices[4];
device_infos_t known_devices = {4, 0, fake_devices};
struct host_status g_host_status[MAX_HOSTS];
volatile int g_host_scan_thread_status = 0;
volatile int g_host_status_changed = 0;
const char *gs_error = "";
CONNECTION_LISTENER_CALLBACKS connection_callbacks;
static int fake_status = LI_DISCONNECTED;
static int fake_running_game = 0;

void vita_debug_log(const char *fmt, ...) {}
void start_host_scan_thread(void) {}
void stop_host_scan_thread(void) {}
int connection_terminate(void) { return 0; }
int connection_get_status(void) { return fake_status; }
int connection_paired(void) { fake_status = LI_PAIRED; return 0; }
int connection_reset(void) { return 0; }
void connection_close_idle(void) {}
void http_set_reuse(bool reuse) {}
int connection_resume(void) { return 0; }
bool connection_is_ready(void) { return false; }
bool connection_is_connected(void) { return false; }
void save_device_info(const device_info_t *info) {}
bool update_device(device_info_t *info) { return true; }
bool remove_device(const char *name) { return true; }
bool rename_device(device_info_t *info, const char *new_name) { return true; }
device_info_t *find_device(const char *name) { return NULL; }
device_info_t *find_device_by_address(const char *address) { return NULL; }
device_info_t *append_device(device_info_t *info) { return info; }
int ime_dialog_string(char *text, const char *title, const char *def) { return -1; }
int ime_dialog_number(char *text, const char *title, const char *def) { return -1; }
bool vitavideo_initialized(void) { return false; }
void vitapower_config(CONFIGURATION c) {}
static int fake_wifi_power_save = 0;
int vitapower_wifi_power_save(void) { return fake_wifi_power_save; }
void vitapower_busy_begin(void) {}
void vitapower_busy_end(void) {}
void LiSetVideoReorderWaitUs(uint32_t wait_us) {}
void vitainput_config(CONFIGURATION c) {}
void ui_search_device(void) {}
int ui_check_ip_update(device_info_t *info, const char *new_ip) { return 0; }
void touchabsolute_enable(bool enable) {}
void keyboardsystem_set_layout(int layout) {}
void stream_stats_set_app(int app_id) {}
bool send_wol_packet(const char *mac, const char *ip_broadcast, int port) { return true; }
int sceKernelGetModelForCDialog(void) { return 0; }
int sceIoMkdir(const char *dir, int mode) { return 0; }
int sceDisplayGetRefreshRate(float *fps) { *fps = 59.94f; return 0; }
PDECODER_RENDERER_CALLBACKS platform_get_video(enum platform system) { return NULL; }
PAUDIO_RENDERER_CALLBACKS platform_get_audio(enum platform system) { return NULL; }
void LiInitializeStreamConfiguration(PSTREAM_CONFIGURATION c) { memset(c, 0, sizeof(*c)); }
int LiStartConnection(PSERVER_INFORMATION a, PSTREAM_CONFIGURATION b, PCONNECTION_LISTENER_CALLBACKS c,
                      PDECODER_RENDERER_CALLBACKS d, PAUDIO_RENDERER_CALLBACKS e, void *f, int g, void *h, int i) { return -1; }
const char *LiGetStageName(int stage) { return ""; }
int connection_stage = 0;

static APP_LIST fake_apps[8];
static const char *fake_app_names[] = {
  "Final Fantasy IX", "Final Fantasy VIII", "Chrono Cross", "DuckStation",
  "Steam Big Picture", "Desktop", "Hollow Knight", "Celeste"
};

int gs_init(PSERVER_DATA server, char *address, unsigned short port, const char *key_dir, int log, bool unsupported) {
  memset(server, 0, sizeof(*server));
  server->paired = true;
  server->gpuType = "Intel(R) Iris(R) Xe Graphics";
  server->serverInfo.address = "192.168.1.24";
  server->httpPort = 47989;
  server->currentGame = fake_running_game;
  return GS_OK;
}
int gs_applist(PSERVER_DATA server, PAPP_LIST *list) {
  for (int i = 0; i < 8; i++) {
    fake_apps[i].name = (char *)fake_app_names[i];
    fake_apps[i].id = 1000 + i;
    fake_apps[i].next = i < 7 ? &fake_apps[i + 1] : NULL;
  }
  *list = fake_apps;
  return GS_OK;
}
void sort_app_list(PAPP_LIST list) {}
int gs_pair(PSERVER_DATA server, char *pin) { return GS_OK; }
int gs_unpair(PSERVER_DATA server) { return GS_OK; }
int gs_start_app(PSERVER_DATA s, PSTREAM_CONFIGURATION c, int id, bool sops, bool local, int mask) { return -1; }
int gs_quit_app(PSERVER_DATA server) { return GS_OK; }
int gs_get_server_mac(PSERVER_DATA server, char *mac, unsigned int size) { return -1; }
void boxart_prefetch(PSERVER_DATA server, PAPP_LIST apps) {}
vita2d_texture *boxart_load_texture(PSERVER_DATA server, int app_id) { return NULL; }
bool stream_stats_read_last(int app_id, stream_last_session *out) {
  if (app_id != 1000 && app_id != 1002)
    return false;
  memset(out, 0, sizeof(*out));
  out->shown_fps_x10 = 599;
  out->r2p_ms_x10 = 142;
  out->lost = app_id == 1002 ? 3 : 0;
  out->seconds = 72 * 60;
  snprintf(out->when, sizeof(out->when), "2026-11-14 21:10");
  return true;
}

/* ---------------- scenarios ---------------- */

static void fake_hosts(void) {
  known_devices.count = 3;
  const char *names[] = {"MINI-PC", "DESKTOP-BUREAU", "Living room PC with a rather long name"};
  const char *ips[] = {"192.168.1.24", "192.168.1.31", "192.168.1.57"};
  for (int i = 0; i < 3; i++) {
    snprintf(fake_devices[i].name, sizeof(fake_devices[i].name), "%s", names[i]);
    snprintf(fake_devices[i].internal, sizeof(fake_devices[i].internal), "%s", ips[i]);
    fake_devices[i].port = 47989;
    fake_devices[i].paired = true;
  }
  snprintf(fake_devices[0].mac, sizeof(fake_devices[0].mac), "AA:BB:CC:DD:EE:FF");
  g_host_status[0].status = HOST_ONLINE;
  g_host_status[1].status = HOST_OFFLINE;
  g_host_status[2].status = HOST_IP_CHANGED;
  snprintf(g_host_status[2].current_ip, sizeof(g_host_status[2].current_ip), "192.168.1.90");
}

static void scenario_home(void) {
  fake_hosts();
  preview_wait(12);
  preview_snap("home_1_host_online");
  preview_press(SCE_CTRL_DOWN);
  preview_snap("home_2_host_offline");
  preview_press(SCE_CTRL_DOWN);
  preview_snap("home_3_host_moved");
  preview_press(SCE_CTRL_DOWN);
  preview_snap("home_4_search");
  preview_press(SCE_CTRL_DOWN);
  preview_press(SCE_CTRL_DOWN);
  preview_snap("home_5_settings");
  /* the host's own menu */
  preview_press(SCE_CTRL_UP); preview_press(SCE_CTRL_UP); preview_press(SCE_CTRL_UP);
  preview_press(SCE_CTRL_UP); preview_press(SCE_CTRL_UP);
  preview_press(SCE_CTRL_SQUARE);
  preview_snap("host_menu");
  preview_press(SCE_CTRL_DOWN); preview_press(SCE_CTRL_DOWN); preview_press(SCE_CTRL_DOWN);
  preview_press(SCE_CTRL_DOWN);
  preview_press(SCE_CTRL_CROSS);
  preview_snap("dialog_confirm");
  preview_wait(4);
  while (1)
    ui_main_menu();
}

/* Wi-Fi power save reported on by the Vita: the advice line turns into a warning. */
static void scenario_home_wifi(void) {
  fake_hosts();
  fake_wifi_power_save = 1;
  preview_wait(12);
  preview_snap("home_6_wifi_power_save");
  preview_wait(2);
  ui_main_menu();
}

static void scenario_home_empty(void) {
  known_devices.count = 0;
  preview_wait(12);
  preview_snap("home_empty");
  preview_wait(2);
  ui_main_menu();
}

static void scenario_settings(void) {
  preview_wait(12);
  preview_snap("settings_1_categories");
  for (int c = 0; c < 6; c++) {
    char name[64];
    preview_press(SCE_CTRL_RIGHT);          /* enter the rows */
    snprintf(name, sizeof(name), "settings_cat%d_a", c + 1);
    preview_snap(name);
    preview_press(SCE_CTRL_DOWN); preview_press(SCE_CTRL_DOWN);
    snprintf(name, sizeof(name), "settings_cat%d_b", c + 1);
    preview_snap(name);
    for (int i = 0; i < 8; i++) preview_press(SCE_CTRL_DOWN);
    snprintf(name, sizeof(name), "settings_cat%d_c_end", c + 1);
    preview_snap(name);
    preview_press(SCE_CTRL_CIRCLE);         /* back to the categories */
    preview_press(SCE_CTRL_DOWN);
  }
  preview_wait(4);
  ui_settings_menu();
}

static void scenario_settings_change(void) {
  /* Performance: switch profile and watch the dependent rows follow */
  preview_wait(12);
  preview_press(SCE_CTRL_RTRIGGER); preview_press(SCE_CTRL_RTRIGGER);
  preview_press(SCE_CTRL_RIGHT);
  preview_snap("settings_profile_smooth");
  preview_press(SCE_CTRL_RIGHT);
  preview_snap("settings_profile_paced");
  preview_press(SCE_CTRL_RIGHT);
  preview_snap("settings_profile_latency");
  preview_press(SCE_CTRL_RIGHT);
  preview_snap("settings_profile_original");
  preview_press(SCE_CTRL_DOWN); preview_press(SCE_CTRL_DOWN);
  preview_press(SCE_CTRL_CROSS);
  preview_snap("settings_profile_custom");
  preview_press(SCE_CTRL_RTRIGGER); preview_press(SCE_CTRL_RTRIGGER); preview_press(SCE_CTRL_RTRIGGER);
  preview_press(SCE_CTRL_RIGHT);
  preview_press(SCE_CTRL_DOWN); preview_press(SCE_CTRL_DOWN); preview_press(SCE_CTRL_DOWN);
  preview_snap("settings_advanced_direct_rows");
  preview_wait(4);
  ui_settings_menu();
}

static void scenario_settings_pages(void) {
  /* Touch: the two pages opened from the settings */
  preview_wait(12);
  for (int i = 0; i < 4; i++) preview_press(SCE_CTRL_RTRIGGER);
  preview_press(SCE_CTRL_RIGHT);
  for (int i = 0; i < 4; i++) preview_press(SCE_CTRL_DOWN);
  preview_press(SCE_CTRL_CROSS);
  preview_snap("settings_page_dead_zone");
  preview_press(SCE_CTRL_DOWN);
  preview_press(SCE_CTRL_CIRCLE);
  preview_press(SCE_CTRL_UP); preview_press(SCE_CTRL_UP);
  preview_press(SCE_CTRL_CROSS);          /* corner shortcuts on */
  preview_press(SCE_CTRL_DOWN);
  preview_press(SCE_CTRL_CROSS);
  preview_snap("settings_page_corners");
  for (int i = 0; i < 9; i++) preview_press(SCE_CTRL_DOWN);
  preview_snap("settings_page_corners_end");
  preview_press(SCE_CTRL_CIRCLE);
  preview_wait(4);
  ui_settings_menu();
}

static void scenario_library(void) {
  SERVER_DATA server;
  PAPP_LIST apps;
  int app_id = 0;
  gs_init(&server, "", 0, "", 0, true);
  gs_applist(&server, &apps);
  preview_wait(12);
  preview_snap("library_1_stats");
  preview_press(SCE_CTRL_RIGHT);
  preview_snap("library_2_never_played");
  preview_press(SCE_CTRL_RIGHT);
  preview_snap("library_3_losses");
  for (int i = 0; i < 5; i++) preview_press(SCE_CTRL_RIGHT);
  preview_snap("library_4_end_of_shelf");
  preview_wait(4);
  fake_status = LI_PAIRED;
  ui_library(&server, apps, "MINI-PC", &app_id);
}

static void scenario_library_running(void) {
  SERVER_DATA server;
  PAPP_LIST apps;
  int app_id = 0;
  fake_running_game = 1000;
  gs_init(&server, "", 0, "", 0, true);
  gs_applist(&server, &apps);
  preview_wait(12);
  preview_snap("library_running_1_resume");
  preview_press(SCE_CTRL_RIGHT);
  preview_snap("library_running_2_other");
  preview_wait(4);
  fake_status = LI_PAIRED;
  ui_library(&server, apps, "MINI-PC", &app_id);
}

static void scenario_dialogs(void) {
  preview_wait(6);
  preview_snap("dialog_error");
  preview_press(SCE_CTRL_CROSS);
  preview_wait(4);
  ui_start();
  display_error(tr("No answer from %s.\nIs the PC on, with Sunshine running?"), "MINI-PC");
  preview_snap("dialog_notice");
  preview_wait(4);
  display_notice(2000, "%s", tr("Wake-up signal sent. The PC needs a moment to start."));
}

static void scenario_pairing(void) {
  preview_snap("pairing_code");   /* the screen is drawn once: capture that frame */
  flash_pairing_code("MINI-PC", "4821", "https://192.168.1.24:47990");
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <scenario> <en|fr|es> <output folder>\n", argv[0]);
    return 2;
  }
  preview_set_output(argv[3]);

  static char *no_args[] = {"preview", NULL};
  config_path = "/nonexistent/moonlight.conf";
  config_parse(1, no_args, &config);
  config.key_dir[0] = 0;
  int lang = !strcmp(argv[2], "fr") ? LANG_FR : (!strcmp(argv[2], "es") ? LANG_ES : LANG_EN);
  config.language = lang;
  i18n_set_language(lang);

  extern void gui_init(void);
  extern int ui_main_menu(void);
  gui_init();

  const char *s = argv[1];
  if (!strcmp(s, "home")) scenario_home();
  else if (!strcmp(s, "home_empty")) scenario_home_empty();
  else if (!strcmp(s, "home_wifi")) scenario_home_wifi();
  else if (!strcmp(s, "pairing")) scenario_pairing();
  else if (!strcmp(s, "settings_pages")) scenario_settings_pages();
  else if (!strcmp(s, "settings")) scenario_settings();
  else if (!strcmp(s, "settings_change")) scenario_settings_change();
  else if (!strcmp(s, "library")) scenario_library();
  else if (!strcmp(s, "library_running")) scenario_library_running();
  else if (!strcmp(s, "dialogs")) scenario_dialogs();
  else {
    fprintf(stderr, "unknown scenario %s\n", s);
    return 2;
  }
  return 0;
}
