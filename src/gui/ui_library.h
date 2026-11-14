/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Library screen: the host's games as a row of covers (XMB style), the
 * selected one shown large with the measurements of its last session.
 */
#pragma once

#include "client.h"

enum {
  LIBRARY_BACK = 0,     /* circle: leave the host */
  LIBRARY_LAUNCH,       /* cross: start or resume *app_id */
  LIBRARY_OPTIONS,      /* triangle: classic menu (pairing, disconnect, ...) */
  LIBRARY_QUIT_APP,     /* square: quit the game running on the host */
  LIBRARY_SETTINGS,     /* start: settings */
};

int ui_library(PSERVER_DATA server, PAPP_LIST apps, const char *host_name, int *app_id);
