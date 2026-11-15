/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer, Sunguk Lee
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

bool vitapower_init();
void vitapower_config(CONFIGURATION config);

/* The Vita system setting "Use Wi-Fi in Power Save Mode", read from the
 * registry: 1 on, 0 off, -1 if it cannot be read. With it on, the Wi-Fi chip
 * sleeps between packets, which adds delay to a stream. */
int vitapower_wifi_power_save(void);

void vitapower_start();
void vitapower_stop();

/* Around work that precedes a stream (reaching the PC, pairing, list of games,
 * stream start): clocks boosted meanwhile, when "Maximum CPU and GPU speed" is
 * on. Calls nest. */
void vitapower_busy_begin(void);
void vitapower_busy_end(void);
