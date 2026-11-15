/* UI preview: scenario script helpers (see preview_sys.c). */
#pragma once

void preview_set_output(const char *dir);
void preview_wait(int frames);
void preview_press(unsigned int buttons);
void preview_tap(int x, int y);
void preview_snap(const char *name);
