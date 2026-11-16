#ifndef KEYBOARDKEYS_H
#define KEYBOARDKEYS_H

#include <wchar.h>

// Maps a character to a virtual key + shift
struct CharVKMap {
    wchar_t ch;
    int vk;
    int needs_shift;
};

// Supported layouts
typedef enum {
    KB_LAYOUT_EN_US = 0,
    KB_LAYOUT_ES_ES,
    KB_LAYOUT_ES_LATAM,
    // Characters sent as text (UTF-8), typed on the PC whatever its keyboard
    // layout; keys such as Enter or Backspace are still sent as keys.
    KB_LAYOUT_TEXT,
    KB_LAYOUT_COUNT
} KeyboardLayout;

// Returns the dictionary and its size for the selected layout
const struct CharVKMap* get_char_vk_dict(KeyboardLayout layout, int* out_count);

void keyboardsystem_set_layout(KeyboardLayout layout);

#endif // KEYBOARDKEYS_H
