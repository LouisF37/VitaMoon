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

#include "http.h"
#include "errors.h"

#include <stdbool.h>
#include <string.h>
#include <curl/curl.h>
#include <pthread.h>

static CURL *curl;
/* One curl handle is shared by every request; the box-art downloader runs on its
 * own thread, so requests are serialised. */
static pthread_mutex_t curl_lock = PTHREAD_MUTEX_INITIALIZER;

static bool debug;

static size_t _write_curl(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  PHTTP_DATA mem = (PHTTP_DATA)userp;

  mem->memory = realloc(mem->memory, mem->size + realsize + 1);
  if(mem->memory == NULL)
    return 0;

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

/* Longest wait for the PC to accept a connection (TCP and TLS handshake). A PC
 * that is off or asleep never answers; without this limit a request to it
 * blocked the menu for curl's default of up to 300 s. On a home network the
 * connection takes milliseconds; 3 s as in juanjsebgarcia/vita-moonlight. */
#define CONNECT_TIMEOUT_S 3L

static bool reuse_connections = false;

/* Keep the connections to the PC open between requests, and resume TLS
 * sessions instead of a full handshake each time (off by default: the original
 * port closes them after every request). */
void http_set_reuse(bool reuse) {
  reuse_connections = reuse;
}

int http_init(const char* keyDirectory, int logLevel) {
  pthread_mutex_lock(&curl_lock);
  /* called once per connection to a PC: release the handle of the previous one */
  if (curl)
    curl_easy_cleanup(curl);
  curl = curl_easy_init();
  debug = logLevel >= 2;
  if (!curl) {
    pthread_mutex_unlock(&curl_lock);
    return GS_FAILED;
  }

  char certificateFilePath[4096];
  snprintf(certificateFilePath, sizeof(certificateFilePath), "%s/%s", keyDirectory, CERTIFICATE_FILE_NAME);

  char keyFilePath[4096];
  snprintf(keyFilePath, sizeof(keyFilePath), "%s/%s", keyDirectory, KEY_FILE_NAME);

  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_SSLENGINE_DEFAULT, 1L);
  curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE,"PEM");
  curl_easy_setopt(curl, CURLOPT_SSLCERT, certificateFilePath);
  curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
  curl_easy_setopt(curl, CURLOPT_SSLKEY, keyFilePath);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_curl);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, reuse_connections ? 1L : 0L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);

  pthread_mutex_unlock(&curl_lock);
  return GS_OK;
}

static int http_request_unlocked(char* url, PHTTP_DATA data, long timeout_s);

int http_request(char* url, PHTTP_DATA data) {
  return http_request_timeout(url, data, HTTP_REQUEST_TIMEOUT);
}

int http_request_timeout(char* url, PHTTP_DATA data, long timeout_s) {
  pthread_mutex_lock(&curl_lock);
  int ret = http_request_unlocked(url, data, timeout_s);
  pthread_mutex_unlock(&curl_lock);
  return ret;
}

static int http_request_unlocked(char* url, PHTTP_DATA data, long timeout_s) {
  if (!curl) {
    gs_error = "No connection to the PC";
    return GS_FAILED;
  }
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);
  curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, reuse_connections ? 0L : 1L);

  if (debug)
    printf("Request %s\n", url);

  if (data->size > 0) {
    free(data->memory);
    data->memory = malloc(1);
    if(data->memory == NULL)
      return GS_OUT_OF_MEMORY;

    data->size = 0;
  }
  CURLcode res = curl_easy_perform(curl);

  if(res != CURLE_OK) {
    gs_error = curl_easy_strerror(res);
    return GS_FAILED;
  } else if (data->memory == NULL) {
    return GS_OUT_OF_MEMORY;
  }

  if (debug)
    printf("Response:\n%s\n\n", data->memory);

  return GS_OK;
}

void http_cleanup() {
  pthread_mutex_lock(&curl_lock);
  if (curl)
    curl_easy_cleanup(curl);
  curl = NULL;
  pthread_mutex_unlock(&curl_lock);
}

PHTTP_DATA http_create_data() {
  PHTTP_DATA data = malloc(sizeof(HTTP_DATA));
  if (data == NULL)
    return NULL;

  data->memory = malloc(1);
  if(data->memory == NULL) {
    free(data);
    return NULL;
  }
  data->size = 0;

  return data;
}

void http_free_data(PHTTP_DATA data) {
  if (data != NULL) {
    if (data->memory != NULL)
      free(data->memory);

    free(data);
  }
}
