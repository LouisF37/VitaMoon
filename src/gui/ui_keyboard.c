#include "ui_keyboard.h"
#include "../input/keyboardkeys.h"
#include "ui_settings.h"
#include "guilib.h"
#include "../config.h"
#include <psp2/kernel/clib.h>
#include "../i18n.h"

static const char* keyboard_layout_names[] = {
    "English (US)",
    "Spanish (Spain)",
    "Spanish (Latin America)",
};

static int keyboard_layout_loop(int id, void *context, const input_data *input) {
    if ((input->buttons & config.btn_confirm) && !(input->buttons & SCE_CTRL_HOLD)) {
        // Select the layout
        keyboardsystem_set_layout((KeyboardLayout)id);
        config.keyboard_layout = id;
        sceClibPrintf("[Moonlight] Keyboard layout selected: %s (id=%d)\n", keyboard_layout_names[id], id);
        ui_settings_save_config();
        return 1; // leave the menu
    }
    return 0;
}

int keyboard_layout_menu(void) {
    // Sync the global layout with the config before showing the menu
    keyboardsystem_set_layout((KeyboardLayout)config.keyboard_layout);
    menu_entry menu[KB_LAYOUT_COUNT] = {0}; // all zero
    for (int i = 0; i < KB_LAYOUT_COUNT; ++i) {
        menu[i].name = tr(keyboard_layout_names[i]);
        menu[i].id = i;
        menu[i].disabled = false;
        menu[i].suffix = (i == config.keyboard_layout) ? "*" : "";
    }
    menu_geom geom = make_geom_centered(360, 180);
    int sel = display_menu(menu, KB_LAYOUT_COUNT, &geom, &keyboard_layout_loop, NULL, NULL, menu);
    // The layout is applied in the loop; nothing left to do here
    (void)sel; // unused
    return 0;
}