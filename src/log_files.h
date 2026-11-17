/*
 * The log's files: one per run of the app, <data>/logs/vitamoon-<stamp>.log,
 * the stamp (YYYYMMDD-HHMMSS, local time) being when the app started, as for
 * the statistics files. The newest LOG_KEEP_RUNS are kept. Up to 1.0.0-beta.11
 * the log was <data>/vitamoon.log, with the four runs before it as
 * vitamoon.1.log .. vitamoon.4.log; log.c moves those into logs/ once.
 * Kept free of Vita calls for tools/tests/test_log.c.
 */
#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_info.h"
#include "video/stats_prune.h"

#define LOG_FOLDER "logs"
#define LOG_KEEP_RUNS 30
#define LOG_PREFIX APP_DATA_DIR "-"
#define LOG_SUFFIX ".log"

/* The stamp of a log file's name ("vitamoon-<stamp>.log"), or NULL. */
static inline const char *log_file_stamp(const char *name) {
  size_t prefix = strlen(LOG_PREFIX), suffix = strlen(LOG_SUFFIX), n = strlen(name);
  if (n != prefix + STATS_STAMP_LEN + suffix || strncmp(name, LOG_PREFIX, prefix) != 0 ||
      strcmp(name + n - suffix, LOG_SUFFIX) != 0)
    return NULL;
  const char *stamp = name + prefix;
  for (int i = 0; i < STATS_STAMP_LEN; i++) {
    bool dash = i == 8;
    if (dash ? stamp[i] != '-' : (stamp[i] < '0' || stamp[i] > '9'))
      return NULL;
  }
  return stamp;
}

/* The name of the log of a run started at that moment. */
static inline void log_file_name(char *out, size_t n, int year, int month, int day, int hour,
                                 int minute, int second) {
  snprintf(out, n, LOG_PREFIX "%04d%02d%02d-%02d%02d%02d" LOG_SUFFIX, year % 10000, month % 100,
           day % 100, hour % 100, minute % 100, second % 100);
}

/* Marks the logs of all but the newest `keep` runs; other names never. */
static inline int log_prune_choose(const char **names, int count, int keep, bool *delete_it) {
  return dated_prune_choose(names, count, keep, delete_it, log_file_stamp);
}
