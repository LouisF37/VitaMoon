#include <psp2/io/stat.h>
#include <stdio.h>
#include <string.h>
#include "app_info.h"
#include "debug.h"
#include "config.h"
#include "check_dir.h"

#define SCE_ERROR_ALREADY_EXISTS 0x80010011

// Writes a one-line note next to the data folder, for the cases where it could
// not be created where expected (the debug log is not open yet).
static void write_note(const char *path, const char *message) {
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "[" APP_NAME "] %s\n", message);
    fclose(f);
  }
}

// Creates the data folder in the first place that works and returns it in
// out_key_dir (with a trailing slash) and the settings file in out_path.
// Both buffers must hold at least MOONLIGHT_PATH_MAX bytes.
void check_and_create_data_dir(char *out_path, char *out_key_dir) {
  #ifdef USE_DIR_UMA0
  static const char *const candidates[] = {
    "uma0:data/" APP_DATA_DIR,
  };
  #else
  static const char *const candidates[] = {
    "ux0:data/" APP_DATA_DIR,
    "ux0:" APP_DATA_DIR,
    "uma0:data/" APP_DATA_DIR,
  };
  #endif
  const int count = sizeof(candidates) / sizeof(candidates[0]);

  int found = -1;
  for (int i = 0; i < count; i++) {
    int ret = sceIoMkdir(candidates[i], 0777);
    if (ret >= 0 || ret == SCE_ERROR_ALREADY_EXISTS) {
      found = i;
      vita_debug_log("[" APP_NAME "] Data folder created or found: %s", candidates[i]);
      break;
    }
  }

  if (found >= 0) {
    snprintf(out_key_dir, MOONLIGHT_PATH_MAX, "%s/", candidates[found]);
    snprintf(out_path, MOONLIGHT_PATH_MAX, "%s/" APP_DATA_DIR ".conf", candidates[found]);
    if (found > 0 && strncmp(candidates[found], "uma0:", 5) == 0)
      write_note("uma0:data/" APP_DATA_DIR ".log",
                 "ux0:data/" APP_DATA_DIR " could not be created; using uma0:data/" APP_DATA_DIR ".");
  } else {
    snprintf(out_key_dir, MOONLIGHT_PATH_MAX, "ux0:data/");
    snprintf(out_path, MOONLIGHT_PATH_MAX, "ux0:data/" APP_DATA_DIR ".conf");
    write_note("ux0:data/" APP_DATA_DIR ".log", "No data folder could be created; using ux0:data.");
  }

  // Create the settings file if needed and record the folder in it.
  FILE *conf = fopen(out_path, "a");
  if (conf) fclose(conf);
  conf = fopen(out_path, "r+");
  if (conf) {
    char line[512];
    int has_key_dir = 0;
    while (fgets(line, sizeof(line), conf)) {
      if (strncmp(line, "key_dir = ", 10) == 0) {
        has_key_dir = 1;
        break;
      }
    }
    if (!has_key_dir) {
      fseek(conf, 0, SEEK_END);
      fprintf(conf, "key_dir = %s\n", out_key_dir);
    }
    fclose(conf);
  }
}
