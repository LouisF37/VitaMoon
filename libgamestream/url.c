/*
 * URL helpers for the requests sent to the PC. See url.h.
 */
#include "url.h"

#include <stdbool.h>

size_t gs_url_encode(const char *in, char *out, size_t out_size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;
  if (out_size == 0)
    return 0;
  for (const unsigned char *p = (const unsigned char *)in; in && *p; p++) {
    bool unreserved = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                      (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
    size_t need = unreserved ? 1 : 3;
    if (n + need >= out_size)
      break;
    if (unreserved) {
      out[n++] = (char)*p;
    } else {
      out[n++] = '%';
      out[n++] = hex[*p >> 4];
      out[n++] = hex[*p & 0x0F];
    }
  }
  out[n] = '\0';
  return n;
}
