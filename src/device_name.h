#ifndef DEVICE_NAME_H
#define DEVICE_NAME_H

/* A PC's name is also the name of its folder in the data folder (its pairing
 * keys and settings): it must be a plain folder name. Kept free of Vita calls:
 * tested on a PC (tools/tests/test_misc.c). */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define DEVICE_NAME_FORBIDDEN "/\\:*?\"<>|"

static inline bool device_name_valid(const char *name) {
  return name && name[0] && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 &&
         !strpbrk(name, DEVICE_NAME_FORBIDDEN);
}

/* Makes a name announced on the network (mDNS) usable as a folder name: the
 * forbidden characters become '-', and a name left empty, "." or ".." becomes
 * `fallback`. */
static inline void device_name_sanitize(char *name, size_t size, const char *fallback) {
  if (!name || size == 0)
    return;
  for (char *p = name; *p; p++)
    if (strchr(DEVICE_NAME_FORBIDDEN, *p))
      *p = '-';
  if (!device_name_valid(name)) {
    strncpy(name, fallback, size - 1);
    name[size - 1] = '\0';
  }
}

#endif // DEVICE_NAME_H
