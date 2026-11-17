/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Box art (game covers) for the new library screen.
 *
 * Covers are downloaded from the host in the background, decoded, cropped and
 * scaled to BOXART_W x BOXART_H, and stored ready to upload as raw RGBA in
 * <key_dir>/boxart/<host>/<appid>.cover, so the UI never decodes a PNG while
 * scrolling. A summary of each download goes to <key_dir>/boxart/<host>/index.txt.
 */
#pragma once

#include "client.h"

#include <stdbool.h>
#include <stddef.h>
#include <vita2d.h>

#define BOXART_W 192
#define BOXART_H 256

/* Starts (or restarts) the background download of every app's cover not cached yet. */
void boxart_prefetch(PSERVER_DATA server, PAPP_LIST apps);

/* Texture of a cached cover, or NULL if it is not downloaded yet. Caller frees it. */
vita2d_texture *boxart_load_texture(PSERVER_DATA server, int app_id);

/* Folder of a PC's covers, for boxart_load_texture_from() when the PC is not
 * connected (the home screen keeps it in the PC's home.txt). */
void boxart_cover_dir(PSERVER_DATA server, char *out, size_t len);
vita2d_texture *boxart_load_texture_from(const char *dir, int app_id);
