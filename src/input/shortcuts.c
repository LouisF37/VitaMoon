// shortcuts.c
// Physical button shortcuts for Vita Moonlight
#include "shortcuts.h"
#include <psp2/ctrl.h>
#include <string.h>
#include "../keyboardsystem.h"
#include "../connection.h"
#include "../debug.h"
#include <psp2/kernel/processmgr.h> // sceKernelGetSystemTimeWide (64-bit: implicit int truncated it)

// Returns true if a shortcut ran and the input must be cleared
bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old) {
    // Shortcut: Start + L1 + R1 opens the pause menu
    if ((pad->buttons & SCE_CTRL_START) && (pad->buttons & SCE_CTRL_L1) && (pad->buttons & SCE_CTRL_R1)) {
        vita_debug_log("Shortcut: START+L1+R1 detected, vita.c releases the input and the pause menu opens");
        connection_minimize();
        return true;
    }

    // Shortcut Start+Left: detected in any order, with a time margin
    static bool keyboard_shortcut_blocked = false;
    static uint64_t shortcut_time = 0;
    static int shortcut_state = 0; // 0: nada, 1: uno presionado, 2: ambos presionados
    // (snapshots live in vita.c)
    uint64_t now = sceKernelGetSystemTimeWide();
    bool start_now = (pad->buttons & SCE_CTRL_START);
    bool left_now = (pad->buttons & SCE_CTRL_LEFT);
    bool start_prev = (pad_old->buttons & SCE_CTRL_START);
    bool left_prev = (pad_old->buttons & SCE_CTRL_LEFT);

    // Rising edge of either button
    if ((start_now && !start_prev) || (left_now && !left_prev)) {
        vita_debug_log("Shortcut: rising edge (START=%d, LEFT=%d) at t=%llu", start_now, left_now, now);
        shortcut_time = now;
        shortcut_state = 1;
    }
    // Both pressed within 300 ms: trigger the shortcut
    if (start_now && left_now) {
        if (shortcut_state == 1 && (now - shortcut_time) < 300000) {
            if (!keyboard_shortcut_blocked) {
                vita_debug_log("Shortcut: START+LEFT detected at t=%llu (delta=%llu)", now, now-shortcut_time);
                // Clear the input locally and on the host BEFORE opening the keyboard
                // (snapshots live in vita.c)
                // Required because the call blocks
                memset((void*)pad, 0, sizeof(SceCtrlData));
                LiSendMultiControllerEvent(0, 1, 0, 0, 0, 128, 0, 128, 0);
                vita_debug_log("[SHORTCUT] Overlay active: OPEN keyboard, empty frame sent to the host");
                keyboardsystem_open_keyboard();
                keyboard_shortcut_blocked = true;
                shortcut_state = 0;
                return true;
            }
        }
        shortcut_state = 2;
    }
    if (!start_now && !left_now) {
        if (keyboard_shortcut_blocked || shortcut_state != 0) {
            vita_debug_log("Shortcut: START and LEFT released, state reset");
        }
        keyboard_shortcut_blocked = false;
        shortcut_state = 0;
    }
    return false;
}
