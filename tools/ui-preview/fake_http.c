/*
 * UI preview: libgamestream's HTTP layer, answered as Sunshine 2026.914 answers
 * (src/nvhttp.cpp of that release) instead of going to the network. The real
 * libgamestream (client.c, xml.c) runs on top of it, so the journeys follow
 * what the Vita really sends and how it reads the answers.
 *
 * What it reproduces: serverinfo over HTTP and HTTPS (HTTPS needs a paired
 * client, as Sunshine checks the client certificate during the handshake),
 * applist, launch (refused while a game runs: XML status 400 in an HTTP 200
 * answer), resume (refused when nothing runs: 503), cancel (ends the sessions
 * and the game) and the absent /unpair (404).
 */
#include "preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "errors.h"

extern const char *gs_error;

int http_init(const char *keyDirectory, int logLevel) { return GS_OK; }
void http_set_reuse(bool reuse) {}
void http_cleanup(void) {}

PHTTP_DATA http_create_data(void) {
  PHTTP_DATA data = calloc(1, sizeof(HTTP_DATA));
  if (data)
    data->memory = calloc(1, 1);
  return data;
}

void http_free_data(PHTTP_DATA data) {
  if (data) {
    free(data->memory);
    free(data);
  }
}

static void answer(PHTTP_DATA data, const char *xml) {
  free(data->memory);
  data->memory = strdup(xml);
  data->size = strlen(xml);
}

/* The value of key in the query of url, or "" (copied into out). */
static void query_arg(const char *url, const char *key, char *out, size_t size) {
  out[0] = 0;
  const char *q = strchr(url, '?');
  size_t n = strlen(key);
  while (q && *q) {
    q++;
    if (!strncmp(q, key, n) && q[n] == '=') {
      size_t len = strcspn(q + n + 1, "&");
      snprintf(out, size, "%.*s", (int)len, q + n + 1);
      return;
    }
    q = strchr(q, '&');
  }
}

static int serve(char *url, PHTTP_DATA data) {
  char xml[2048];
  bool https = !strncmp(url, "https://", 8);
  const char *path = strchr(url + (https ? 8 : 7), '/');
  char name[32] = "";
  snprintf(name, sizeof(name), "%.*s", path ? (int)strcspn(path, "?") : 0, path ? path : "");

  /* gs_init() starts with the plain status request: the PC is being opened
   * (at the address the menus chose: "open 192.168.1.24:47989") */
  if (!strcmp(name, "/serverinfo") && !https)
    preview_record("open %.*s", path ? (int)(path - url - 7) : 0, url + 7);
  if (!preview_pc.answers) {
    gs_error = "Couldn't connect to server";
    return GS_FAILED;
  }
  /* Sunshine refuses the TLS handshake of a client it has not paired. */
  if (https && !preview_pc.paired) {
    gs_error = "SSL peer certificate or SSH remote key was not OK";
    return GS_FAILED;
  }

  if (!strcmp(name, "/serverinfo")) {
    int running = preview_pc.running;
    snprintf(xml, sizeof(xml),
             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<root status_code=\"200\"><hostname>MINI-PC</hostname>"
             "<appversion>7.1.431.-1</appversion><GfeVersion>3.23.0.74</GfeVersion>"
             "<uniqueid>9B6F3C7E-0000-0000-0000-000000000000</uniqueid><HttpsPort>47984</HttpsPort>"
             "<ExternalPort>47989</ExternalPort><MaxLumaPixelsHEVC>0</MaxLumaPixelsHEVC>"
             "<mac>%s</mac><LocalIP>192.168.1.24</LocalIP><ServerCodecModeSupport>1</ServerCodecModeSupport>"
             "<PairStatus>%d</PairStatus><currentgame>%d</currentgame><state>%s</state></root>",
             https ? "AA:BB:CC:DD:EE:FF" : "00:00:00:00:00:00", https ? 1 : 0, running,
             running ? "SUNSHINE_SERVER_BUSY" : "SUNSHINE_SERVER_FREE");
    answer(data, xml);
    return GS_OK;
  }
  if (!strcmp(name, "/applist")) {
    static const char *names[] = {"Final Fantasy IX", "Final Fantasy VIII", "Chrono Cross", "DuckStation",
                                  "Steam Big Picture", "Desktop", "Hollow Knight", "Celeste"};
    char *p = xml;
    p += sprintf(p, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<root status_code=\"200\">");
    for (int i = 0; i < 8; i++)
      p += sprintf(p, "<App><IsHdrSupported>0</IsHdrSupported><AppTitle>%s</AppTitle><ID>%d</ID></App>", names[i],
                   1000 + i);
    sprintf(p, "</root>");
    answer(data, xml);
    return GS_OK;
  }
  if (!strcmp(name, "/launch") || !strcmp(name, "/resume")) {
    bool launch = name[1] == 'l';
    char appid[16];
    query_arg(url, "appid", appid, sizeof(appid));
    int id = atoi(appid);
    const char *refusal = NULL;
    int status = 200;
    if (preview_pc.refuse_start > 0) {
      preview_pc.refuse_start--;
      refusal = "Failed to initialize video capture/encoding. Is a display connected and turned on?";
      status = 503;
    } else if (launch && preview_pc.running) {
      refusal = "An app is already running on this host";
      status = 400;
    } else if (!launch && !preview_pc.running) {
      refusal = "No running app to resume";
      status = 503;
    }
    if (refusal) {
      preview_record("%s %d refused", launch ? "launch" : "resume", id);
      snprintf(xml, sizeof(xml),
               "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<root status_code=\"%d\" status_message=\"%s\">"
               "<%s>0</%s></root>", status, refusal, launch ? "gamesession" : "resume", launch ? "gamesession" : "resume");
      answer(data, xml);
      return GS_OK;   /* an HTTP 200 answer, the refusal is in the XML */
    }
    if (launch) {
      preview_pc.running = id;
      preview_record("launch %d", id);
    } else if (id != preview_pc.running) {
      /* Sunshine resumes what runs, whatever the appid */
      preview_record("resume %d while %d runs", id, preview_pc.running);
    } else {
      preview_record("resume %d", id);
    }
    preview_pc.session_ready = true;
    snprintf(xml, sizeof(xml),
             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<root status_code=\"200\"><sessionUrl0>rtsp://192.168.1.24:48010"
             "</sessionUrl0><%s>1</%s></root>", launch ? "gamesession" : "resume", launch ? "gamesession" : "resume");
    answer(data, xml);
    return GS_OK;
  }
  if (!strcmp(name, "/cancel")) {
    if (preview_stream_open())
      preview_record("quit while the stream is open");
    preview_record("quit %d", preview_pc.running);
    preview_pc.running = 0;
    preview_pc.session_ready = false;
    answer(data, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<root status_code=\"200\"><cancel>1</cancel></root>");
    return GS_OK;
  }
  /* /unpair and anything else: Sunshine's not_found, an HTTP 404 that curl
   * (CURLOPT_FAILONERROR) turns into an error */
  gs_error = "HTTP response code said error";
  return GS_FAILED;
}

int http_request(char *url, PHTTP_DATA data) {
  return serve(url, data);
}

int http_request_timeout(char *url, PHTTP_DATA data, long timeout_s) {
  return serve(url, data);
}

int http_request_cancelable(char *url, PHTTP_DATA data, long timeout_s, bool (*cancel)(void)) {
  return serve(url, data);
}
