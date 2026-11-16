/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdbool.h>
#include <stdlib.h>

#define CERTIFICATE_FILE_NAME "client.pem"
#define KEY_FILE_NAME "key.pem"

typedef struct _HTTP_DATA {
  char *memory;
  size_t size;
} HTTP_DATA, *PHTTP_DATA;

int http_init(const char* keyDirectory, int logLevel);
void http_set_reuse(bool reuse);
void http_cleanup();
PHTTP_DATA http_create_data();
/* Seconds a request may take in all, unless it asks for its own limit. */
#define HTTP_REQUEST_TIMEOUT 10
/* Gives up after HTTP_REQUEST_TIMEOUT seconds. */
int http_request(char* url, PHTTP_DATA data);
/* As http_request, with its own limit in seconds (0 waits for ever). */
int http_request_timeout(char* url, PHTTP_DATA data, long timeout_s);
/* As http_request_timeout, and cancel() is asked at least once a second while the
 * request waits (on the calling thread): true stops it, GS_CANCELLED. */
int http_request_cancelable(char* url, PHTTP_DATA data, long timeout_s, bool (*cancel)(void));
void http_free_data(PHTTP_DATA data);
