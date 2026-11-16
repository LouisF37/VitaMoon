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
#include "quickmenu.h"
#include "client.h"
#include "errors.h"
#include "i18n.h"
#include "platform.h"
#include "gui/guilib.h"
#include "gui/theme.h"
int ui_main_menu(void);
#include "gui/ui_settings.h"
#include "gui/ui_library.h"
#include "gui/ui_connect.h"
#include "gui/moon.h"
#include "reconnect.h"
#include "gui/boxart.h"
#include "video/stream_stats.h"

/* ---------------- stand-ins for the rest of the app ---------------- */

static device_info_t fake_devices[4];
device_infos_t known_devices = {4, 0, fake_devices};
struct host_status g_host_status[MAX_HOSTS];
volatile int g_host_scan_thread_status = 0;
volatile int g_host_status_changed = 0;
const char *gs_error = "";
int gs_status_code = 0;
CONNECTION_LISTENER_CALLBACKS connection_callbacks;
static int fake_status = LI_DISCONNECTED;
static int fake_running_game = 0;

void vita_debug_log(const char *fmt, ...) {}
void start_host_scan_thread(void) {}
void stop_host_scan_thread(void) {}
int connection_terminate(void) { return 0; }
int connection_get_status(void) { return fake_status; }
int connection_paired(void) { fake_status = LI_PAIRED; return 0; }
/* Same transitions as src/connection.c for the states the menus go through. */
int connection_reset(void) {
  if (fake_status != LI_DISCONNECTED)
    return -1;
  fake_status = LI_READY;
  return 0;
}
void connection_set_stage_hook(void (*hook)(int stage)) { (void)hook; }
void connection_close_idle(void) {}
bool connection_was_lost(void) { return false; }
int connection_last_error(void) { return 0; }
const char *connection_error_name(int error_code) { return "preview"; }
void http_set_reuse(bool reuse) {}
int connection_resume(void) { return 0; }
int connection_minimize(void) { return 0; }
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
static int fake_bluetooth = 0;
int vitapower_bluetooth(void) { return fake_bluetooth; }
void vitapower_busy_begin(void) {}
void vitapower_busy_end(void) {}
void LiSetVideoReorderWaitUs(uint32_t wait_us) {}
void vitainput_config(CONFIGURATION c) {}
void ui_search_device(void) {}
int ui_check_ip_update(device_info_t *info, const char *new_ip) { return 0; }
void touchabsolute_enable(bool enable) {}
void keyboardsystem_set_layout(int layout) {}
void keyboardsystem_open_keyboard(void) {}
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
int gs_pair(PSERVER_DATA server, char *pin, bool (*cancel)(void)) { return GS_OK; }
int gs_unpair(PSERVER_DATA server) { return GS_OK; }
static int stream_asked = 0;   /* app id the PC was asked to start (scenario play) */
int gs_start_app(PSERVER_DATA s, PSTREAM_CONFIGURATION c, int id, bool sops, bool local, int mask) {
  stream_asked = id;
  gs_error = "preview: no PC";
  return -1;
}
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

/* Bluetooth on: the advice line warns about the Wi-Fi. */
static void scenario_home_bt(void) {
  fake_hosts();
  fake_bluetooth = 1;
  preview_wait(12);
  preview_snap("home_7_bluetooth");
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
  /* General: change the theme twice. The setting is applied in the middle of a
   * frame that has already drawn the old background (it froze beta.5 on a
   * Vita); fake_vita2d.c breaks the preview if that background is freed too
   * early. */
  preview_press(SCE_CTRL_CIRCLE);
  for (int i = 0; i < 5; i++) preview_press(SCE_CTRL_LTRIGGER);
  preview_press(SCE_CTRL_RIGHT);
  preview_press(SCE_CTRL_DOWN);
  preview_press(SCE_CTRL_RIGHT);
  preview_snap("settings_theme_changed");
  preview_press(SCE_CTRL_LEFT);
  preview_snap("settings_theme_back");
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

/* Play from the library, the way the Vita gets there: open a paired PC from the
 * home screen, then X on a cover. The PC must be asked to start the game (here
 * it refuses, and the error is shown); until beta.7 nothing happened. */
static void check_play_asked(void) {
  if (!stream_asked)
    fprintf(stderr, "preview: Play in the library did not ask the PC to start the game\n");
}

static void scenario_play(void) {
  atexit(check_play_asked);
  device_info_t pc;
  memset(&pc, 0, sizeof(pc));
  snprintf(pc.name, sizeof(pc.name), "MINI-PC");
  snprintf(pc.internal, sizeof(pc.internal), "192.168.1.24");
  pc.port = 47989;
  pc.paired = true;
  fake_status = LI_DISCONNECTED;
  preview_wait(12);
  preview_snap("play_1_library");
  preview_press(SCE_CTRL_CROSS);
  preview_snap("play_2_started");
  preview_wait(4);
  ui_connect_paired_device(&pc);
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
  preview_press(SCE_CTRL_CROSS);
  preview_wait(8);
  preview_snap("library_running_3_switch");
  preview_press(SCE_CTRL_CIRCLE);
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

/* Resume after a setting that needs a new stream was changed during the pause. */
static void scenario_restart(void) {
  preview_wait(6);
  preview_snap("dialog_restart");
  preview_press(SCE_CTRL_CIRCLE);
  ui_start();
  display_confirm(tr("Some settings you changed only apply to a new stream.\nRestart the stream now? The game keeps running on the PC."));
}

/* Automatic reconnection after a lost stream: the three states of its screen,
 * then the message when it gives up. Every step is queued first: the script
 * ends with its last step. */
static void scenario_reconnect(void) {
  preview_snap("reconnect_1_wifi");
  preview_snap("reconnect_2_first");
  preview_snap("reconnect_3_retry");
  preview_wait(2);
  preview_snap("reconnect_4_failed");
  preview_press(SCE_CTRL_CROSS);

  /* As reconnect_stream() draws them: the moon fills over the minute of tries. */
  char message[256];
  flash_moon(moon_phase(0, RECONNECT_GIVE_UP_MS), tr("Connection lost"), tr("Waiting for the Wi-Fi..."), true);
  snprintf(message, sizeof(message), tr("Reconnecting to %s..."), "MINI-PC");
  flash_moon(moon_phase(1500, RECONNECT_GIVE_UP_MS), tr("Connection lost"), message, true);
  snprintf(message, sizeof(message), tr("Reconnecting to %s (try %d)..."), "MINI-PC", 3);
  flash_moon(moon_phase(27000, RECONNECT_GIVE_UP_MS), tr("Connection lost"), message, true);
  ui_start();
  display_error(tr("Could not reconnect.\nThe game should still be running on the PC: connect again to resume it."));
}

/* In-game quick menu over a picture standing in for the game (the LiveArea
 * background): opened, statistics turned on, then the question of Quit. */
static void scenario_quickmenu(void) {
  preview_snap("quickmenu_1_open");
  preview_snap("quickmenu_2_stats");
  preview_snap("quickmenu_3_quit");
  preview_wait(1);

  vita2d_texture *game = vita2d_load_PNG_file(ASSET_PREFIX "../sce_sys/livearea/contents/bg.png");
  const unsigned combo = SCE_CTRL_SELECT | SCE_CTRL_L1 | SCE_CTRL_R1;
  static const unsigned script[3][6] = {
    {combo, 0},
    {SCE_CTRL_DOWN, 0, SCE_CTRL_CROSS, 0},
    {SCE_CTRL_UP, 0, SCE_CTRL_UP, 0, SCE_CTRL_CROSS, 0},
  };
  static const int lengths[3] = {2, 4, 6};
  quickmenu_set_context("MINI-PC", "Final Fantasy IX");
  quickmenu_reset();
  config.show_fps = false;
  for (int f = 0; f < 4; f++) {
    for (int i = 0; f < 3 && i < lengths[f]; i++)
      quickmenu_handle_input(script[f][i]);
    ui_start();
    if (game)
      vita2d_draw_texture_scale(game, 0, 0, 960.0f / 840.0f, 544.0f / 500.0f);
    quickmenu_draw();
    ui_end();
  }
}

/* Stream start: the moon before the PC is asked for the game, then at a few
 * stages (2: finding the PC, 4: RTSP, 7: controls, 9: video, the full moon). */
static void scenario_connecting(void) {
  static const int stages[] = {0, 2, 4, 7, 9};
  char name[32];
  for (int i = 0; i < 5; i++) {
    snprintf(name, sizeof(name), "connecting_%d", stages[i]);
    preview_snap(name);
  }
  for (int i = 0; i < 5; i++)
    ui_connect_preview_moon("MINI-PC", stages[i]);
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
  config_path = "/nonexistent/vitamoon.conf";
  config_parse(1, no_args, &config);
  config.key_dir[0] = 0;
  int lang = !strcmp(argv[2], "fr") ? LANG_FR : (!strcmp(argv[2], "es") ? LANG_ES : LANG_EN);
  config.language = lang;
  i18n_set_language(lang);
  // Colour theme (THEME_* index), for the theme pages of run.sh.
  const char *theme = getenv("PREVIEW_THEME");
  if (theme)
    config.theme = atoi(theme);

  extern void gui_init(void);
  extern int ui_main_menu(void);
  gui_init();

  const char *s = argv[1];
  if (!strcmp(s, "home")) scenario_home();
  else if (!strcmp(s, "home_empty")) scenario_home_empty();
  else if (!strcmp(s, "home_wifi")) scenario_home_wifi();
  else if (!strcmp(s, "home_bt")) scenario_home_bt();
  else if (!strcmp(s, "pairing")) scenario_pairing();
  else if (!strcmp(s, "settings_pages")) scenario_settings_pages();
  else if (!strcmp(s, "settings")) scenario_settings();
  else if (!strcmp(s, "settings_change")) scenario_settings_change();
  else if (!strcmp(s, "library")) scenario_library();
  else if (!strcmp(s, "library_running")) scenario_library_running();
  else if (!strcmp(s, "play")) scenario_play();
  else if (!strcmp(s, "dialogs")) scenario_dialogs();
  else if (!strcmp(s, "restart")) scenario_restart();
  else if (!strcmp(s, "reconnect")) scenario_reconnect();
  else if (!strcmp(s, "quickmenu")) scenario_quickmenu();
  else if (!strcmp(s, "connecting")) scenario_connecting();
  else {
    fprintf(stderr, "unknown scenario %s\n", s);
    return 2;
  }
  return 0;
}
