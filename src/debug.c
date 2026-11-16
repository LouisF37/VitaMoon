/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2017 Sunguk Lee
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

#include <stdarg.h>
#include <stdbool.h>
#include "debug.h"
#include "log.h"

/* Kept for the many detail lines of the original code and of libgamestream:
 * they go to the detailed log (src/log.c), written only when the setting
 * "Detailed log" is on. */
bool vita_debug_init() {
  return true;
}

void vita_debug_log(const char *s, ...) {
  va_list va;
  va_start(va, s);
  log_vwrite(LOG_DEBUG, "detail", s, va);
  va_end(va);
}
