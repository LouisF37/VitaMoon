#pragma once
#include <stddef.h>

/* Help: what the buttons and the touch screen do during a stream, how to add
 * games to Sunshine, and what helps the Wi-Fi. The home screen shows one card
 * per topic; the page shows them in full. */
enum { HELP_STREAM, HELP_TOUCH, HELP_GAMES, HELP_WIFI, HELP_COUNT };

/* Reads what the Wi-Fi topic reports (power save, Bluetooth): when the home
 * opens or comes back, not every frame. */
void ui_help_refresh(void);
const char *ui_help_title(int topic);
/* The home's card of a topic: a value in large type, up to two lines under it,
 * and whether the value is a warning. */
void ui_help_card(int topic, char *value, size_t value_n, char *line1, size_t n1, char *line2, size_t n2, int *warn);
/* The page, open on `topic`; returns when the user leaves it. */
void ui_help(int topic);
