/*
 * The home screen's file of each PC: <data folder>/<PC name>/home.txt (format
 * and contents in home_logic.h). The PC's folder also holds its pairing, so the
 * file follows the PC when it is renamed and goes when it is forgotten.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "home_logic.h"

/* Reads the file of the PC named `host`; false when it has none (its library
 * was never left on this version) or it cannot be read. */
bool home_data_load(const char *host, home_games *out);

/* Writes it, only if it differs from the file already there. */
void home_data_save(const char *host, const home_games *games);
