#ifndef TEXT_COVER_H
#define TEXT_COVER_H

/* Whether a font has every character of a UTF-8 text, from the table of the
 * code points it has (sorted ranges, as tools/art/make_fonts.py writes them).
 * Kept free of Vita calls: tested on a PC (tools/tests/test_ui.c). */

#include <stdbool.h>

/* The code point at *s, moving *s past it. A malformed sequence counts as
 * U+FFFD, which no table holds, and moves one byte. */
static inline unsigned int utf8_next(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  unsigned int c = p[0];
  int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
  if (extra < 0) {
    *s += 1;
    return 0xFFFD;
  }
  unsigned int cp = extra == 0 ? c : c & (0x3F >> extra);
  for (int i = 1; i <= extra; i++) {
    if ((p[i] & 0xC0) != 0x80) {
      *s += 1;
      return 0xFFFD;
    }
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  *s += 1 + extra;
  return cp;
}

static inline bool text_covered(const char *text, const unsigned int (*ranges)[2], int count) {
  const char *p = text ? text : "";
  while (*p) {
    unsigned int c = utf8_next(&p);
    if (c == '\n')
      continue;
    int lo = 0, hi = count - 1;
    bool found = false;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (c < ranges[mid][0])
        hi = mid - 1;
      else if (c > ranges[mid][1])
        lo = mid + 1;
      else {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }
  return true;
}

#endif // TEXT_COVER_H
