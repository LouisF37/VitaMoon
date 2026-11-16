#ifndef LIBRARY_LOGIC_H
#define LIBRARY_LOGIC_H

/* Library: when a game was last played, said simply, and the order of the
 * shelf. Kept free of Vita calls: tested on a PC (tools/tests/test_library.c).
 * The drawing is in ui_library.c. */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* A moment as written in stats/last_<app>.txt: "YYYY-MM-DD HH:MM". */
typedef struct {
  int year, month, day, hour, minute;
} lib_moment;

static inline bool lib_parse_moment(const char *text, lib_moment *out) {
  lib_moment m;
  if (!text || sscanf(text, "%4d-%2d-%2d %2d:%2d", &m.year, &m.month, &m.day, &m.hour, &m.minute) != 5)
    return false;
  if (m.year < 2000 || m.year > 2200 || m.month < 1 || m.month > 12 || m.day < 1 || m.day > 31 ||
      m.hour < 0 || m.hour > 23 || m.minute < 0 || m.minute > 59)
    return false;
  *out = m;
  return true;
}

/* Days since 1970-01-01 of a calendar date (proleptic Gregorian). */
static inline long lib_days(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

typedef enum {
  LIB_WHEN_TODAY,       /* "today at 19:46" */
  LIB_WHEN_YESTERDAY,   /* "yesterday at 19:46" */
  LIB_WHEN_DAYS_AGO,    /* "3 days ago" (2 to 6) */
  LIB_WHEN_DATE         /* "on 14/11/2026" (a week or more, or a clock set back) */
} lib_when_kind;

/* How to say when `then` was, seen from `today` (only its date is used). */
static inline lib_when_kind lib_when(const lib_moment *then, const lib_moment *today, int *days_ago) {
  long diff = lib_days(today->year, today->month, today->day) - lib_days(then->year, then->month, then->day);
  if (days_ago)
    *days_ago = (int)diff;
  if (diff == 0)
    return LIB_WHEN_TODAY;
  if (diff == 1)
    return LIB_WHEN_YESTERDAY;
  if (diff >= 2 && diff <= 6)
    return LIB_WHEN_DAYS_AGO;
  return LIB_WHEN_DATE;
}

/* Length of a session: minutes under an hour, else hours and minutes. */
static inline void lib_duration_parts(unsigned int seconds, unsigned int *hours, unsigned int *minutes) {
  unsigned int total = (seconds + 59) / 60;   /* a started minute counts */
  if (total == 0)
    total = 1;
  *hours = total / 60;
  *minutes = total % 60;
}

/* Order of the shelf: favourites first, then the most recently played, then
 * the games never played from this Vita in the PC's own order. `when` is the
 * "YYYY-MM-DD HH:MM" text ("" when never played), which sorts like time. */
typedef struct {
  bool favourite;
  const char *when;
  int pc_order;   /* position in the PC's list */
} lib_sort_key;

static inline int lib_compare(const lib_sort_key *a, const lib_sort_key *b) {
  if (a->favourite != b->favourite)
    return a->favourite ? -1 : 1;
  bool pa = a->when && *a->when, pb = b->when && *b->when;
  if (pa != pb)
    return pa ? -1 : 1;
  if (pa && pb) {
    int c = strcmp(a->when, b->when);
    if (c != 0)
      return c > 0 ? -1 : 1;   /* later first */
  }
  return a->pc_order < b->pc_order ? -1 : (a->pc_order > b->pc_order ? 1 : 0);
}

#endif // LIBRARY_LOGIC_H
