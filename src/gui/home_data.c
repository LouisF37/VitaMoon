/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * The home screen's file of each PC; see home_data.h.
 */
#include "home_data.h"
#include "../config.h"
#include "../log.h"

#include <stdio.h>
#include <string.h>

#define HOME_FILE_MAX 4096   /* 12 + 4 lines of at most ~130 bytes, and the header */

static void home_path(const char *host, char *out, size_t n) {
  size_t l = strlen(config.key_dir);
  const char *sep = l && config.key_dir[l - 1] == '/' ? "" : "/";
  snprintf(out, n, "%.400s%s%.120s/home.txt", config.key_dir, sep, host);
}

/* Whole file into buf (NUL-terminated); its length, or -1. */
static int read_file(const char *path, char *buf, size_t n) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  size_t got = fread(buf, 1, n - 1, f);
  fclose(f);
  buf[got] = 0;
  return (int)got;
}

bool home_data_load(const char *host, home_games *out) {
  static char text[HOME_FILE_MAX];   /* menus thread only */
  char path[640];
  memset(out, 0, sizeof(*out));
  if (!host || !host[0])
    return false;
  home_path(host, path, sizeof(path));
  if (read_file(path, text, sizeof(text)) < 0)
    return false;
  return home_games_parse(text, out);
}

void home_data_save(const char *host, const home_games *games) {
  static char text[HOME_FILE_MAX], old[HOME_FILE_MAX];   /* menus thread only */
  char path[640];
  if (!host || !host[0])
    return;
  int len = home_games_format(games, text, sizeof(text));
  if (len < 0)
    return;
  home_path(host, path, sizeof(path));
  int old_len = read_file(path, old, sizeof(old));
  if (old_len == len && memcmp(old, text, (size_t)len) == 0)
    return;   /* nothing new: the memory card is left alone */
  FILE *f = fopen(path, "wb");
  if (!f) {
    LOGW("home", "%s cannot be written", path);
    return;
  }
  size_t put = fwrite(text, 1, (size_t)len, f);
  if (fclose(f) != 0 || put != (size_t)len)
    LOGW("home", "%s not fully written", path);
}
