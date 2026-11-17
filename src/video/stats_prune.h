/*
 * Which statistics files to delete: every stream writes session-<stamp>.txt and
 * session-<stamp>.csv in <data>/stats (about 1 MB an hour), never deleted
 * before, so the folder grew without end. The newest STATS_KEEP_SESSIONS stamps
 * are kept; the stamp (YYYYMMDD-HHMMSS) sorts as the date does.
 * Kept free of Vita calls for tools/tests/test_misc.c.
 */
#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define STATS_KEEP_SESSIONS 30
#define STATS_STAMP_LEN 15   /* YYYYMMDD-HHMMSS */

/* The stamp of a session file name ("session-<stamp>.txt" or ".csv"), or NULL. */
static inline const char *stats_session_stamp(const char *name) {
  size_t n = strlen(name);
  if (n != 8 + STATS_STAMP_LEN + 4 || strncmp(name, "session-", 8) != 0)
    return NULL;
  if (strcmp(name + n - 4, ".txt") != 0 && strcmp(name + n - 4, ".csv") != 0)
    return NULL;
  return name + 8;
}

static int stats_compare_names(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Sorts names (count entries) and marks in delete[] the files of all but the
 * newest `keep` stamps, a file's stamp being given by stamp_of() (NULL: not one
 * of these files, never marked). Stamps of STATS_STAMP_LEN characters sort as
 * dates do. Returns how many are marked. Used for the logs too (log_files.h). */
static inline int dated_prune_choose(const char **names, int count, int keep, bool *delete_it,
                                     const char *(*stamp_of)(const char *name)) {
  qsort(names, count, sizeof(names[0]), stats_compare_names);
  int marked = 0, stamps = 0;
  const char *last = NULL;
  for (int i = count - 1; i >= 0; i--) {   /* newest first */
    delete_it[i] = false;
    const char *stamp = stamp_of(names[i]);
    if (!stamp)
      continue;
    if (!last || strncmp(stamp, last, STATS_STAMP_LEN) != 0) {
      stamps++;
      last = stamp;
    }
    if (stamps > keep) {
      delete_it[i] = true;
      marked++;
    }
  }
  return marked;
}

/* The statistics files of all but the newest `keep` streams. */
static inline int stats_prune_choose(const char **names, int count, int keep, bool *delete_it) {
  return dated_prune_choose(names, count, keep, delete_it, stats_session_stamp);
}
