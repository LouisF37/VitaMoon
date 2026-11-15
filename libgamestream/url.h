/*
 * URL helpers for the requests sent to the PC.
 */
#ifndef LIBGAMESTREAM_URL_H
#define LIBGAMESTREAM_URL_H

#include <stddef.h>

/* Percent-encodes `in` into `out` (RFC 3986: letters, digits and "-_.~" stay,
 * every other byte becomes %XX, each byte of a UTF-8 character included). Stops
 * before a character that would not fit, so the result never ends in the middle
 * of an escape. Returns the length written; out is always terminated when
 * out_size > 0. */
size_t gs_url_encode(const char *in, char *out, size_t out_size);

#endif
