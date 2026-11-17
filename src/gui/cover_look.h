/*
 * What stands in for a game's cover until it is downloaded: a coloured tile
 * with the game's initials. Shared by the library and the home screen, so a
 * game has the same colour on both.
 */
#pragma once

#include <ctype.h>
#include <vita2d.h>

static const unsigned int k_cover_tints[] = {
  RGBA8(0x3a, 0x6f, 0xb0, 0xFF), RGBA8(0x2f, 0x8f, 0x9a, 0xFF), RGBA8(0xb0, 0x6a, 0x3a, 0xFF),
  RGBA8(0x6b, 0x5a, 0xa8, 0xFF), RGBA8(0x4a, 0x8a, 0x5a, 0xFF), RGBA8(0xa4, 0x55, 0x74, 0xFF),
};

static inline unsigned int cover_tint(int app_id) {
  return k_cover_tints[(unsigned)app_id % (sizeof(k_cover_tints) / sizeof(k_cover_tints[0]))];
}

/* Up to three initials of the words of a name ("Final Fantasy IX" -> "FFI"). */
static inline void cover_initials(const char *name, char out[4]) {
  int n = 0;
  int start = 1;
  for (const char *p = name; p && *p && n < 3; p++) {
    if (*p == ' ' || *p == '-' || *p == ':') {
      start = 1;
    } else if (start && isalnum((unsigned char)*p)) {
      out[n++] = (char)toupper((unsigned char)*p);
      start = 0;
    } else {
      start = 0;
    }
  }
  if (n == 0)
    out[n++] = '?';
  out[n] = 0;
}
