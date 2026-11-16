// keyboardsystem.c
// Opens the PS Vita IME keyboard without a text box
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/libime.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>
#include <psp2/kernel/clib.h>
#include <string.h>
#include "Limelight.h"
#include "input/keyboardkeys.h"
#include "config.h"
#include "../src/debug.h"
#include "log.h"

#define WORK_BUFFER_SIZE (SCE_IME_WORK_BUFFER_SIZE)

static uint8_t work_buffer[WORK_BUFFER_SIZE];
// static SceWChar16 input_text_dummy[1];   // unused
static SceWChar16 output_text[4];      // receives the typed text (never displayed)
#include "input/keyboard.h"
#include <stddef.h>

// Selected layout (text by default)
static KeyboardLayout g_keyboard_layout = KB_LAYOUT_TEXT;
_Static_assert(KB_LAYOUT_COUNT == CONFIG_KEYBOARD_LAYOUT_COUNT && KB_LAYOUT_TEXT == CONFIG_KEYBOARD_TEXT,
               "config.h keyboard constants");


// Internal state of the on-screen keyboard overlay
static bool _keyboard_overlay_open = false;

void keyboardsystem_set_layout(KeyboardLayout layout) {
    g_keyboard_layout = layout;
}

// Expose the state to other modules
bool keyboardsystem_is_open(void) {
    return _keyboard_overlay_open;
}

static int find_vk_for_char(wchar_t ch, int* vk, int* needs_shift) {
    int count = 0;
    const struct CharVKMap* dict = get_char_vk_dict(g_keyboard_layout, &count);
    for (int i = 0; i < count; ++i) {
        if (dict[i].ch == ch) {
            *vk = dict[i].vk;
            *needs_shift = dict[i].needs_shift;
            return 1;
        }
    }
    return 0;
}

/* Text mode: one character sent as UTF-8 (Sunshine types it as a character,
 * whatever the PC's keyboard layout; NowaLone/vita-moonlight sends text too). */
static void send_text_char(unsigned ch) {
    char utf8[3];
    int n;
    if (ch < 0x80) {
        utf8[0] = (char)ch;
        n = 1;
    } else if (ch < 0x800) {
        utf8[0] = (char)(0xC0 | (ch >> 6));
        utf8[1] = (char)(0x80 | (ch & 0x3F));
        n = 2;
    } else {
        utf8[0] = (char)(0xE0 | (ch >> 12));
        utf8[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (ch & 0x3F));
        n = 3;
    }
    int ret = LiSendUtf8TextEvent(utf8, (unsigned)n);
    if (ret != 0)
        vita_debug_log("[IME] text U+%04X not sent (%d)", ch, ret);
}

static int prev_caret_index = 1;
static int logical_caret = 0;
static SceWChar16 ime_working_buffer[4] = {1, 0, 0, 0};
static SceImeCaret caret_rev;
static int caret_toggle = 0;
static int ime_just_opened = 1;
static int forzar_centro = 0;

static void keyboardsystem_ime_event_handler(void *arg, const SceImeEventData *e) {
    // Extended log, as in ime_test.c
    vita_debug_log("[IME] IME event id=%d, caretIndex=%d, buffer=[%04X %04X %04X %04X]", e->id, e->param.caretIndex, output_text[0], output_text[1], output_text[2], output_text[3]);
    int caret = e->param.caretIndex;
    // --- IGNORE the first delete event after opening the keyboard if ch==0 ---
    if (ime_just_opened && e->id == 1 && caret == 0) {
        wchar_t ch = 0;
        for (int i = 0; i < 4; ++i) {
            if (output_text[i] != 0 && output_text[i] != 1) {
                ch = output_text[i];
                break;
            }
        }
        if (ch == 0) {
            vita_debug_log("[IME] First delete event after opening the keyboard ignored (ch==0).");
            ime_just_opened = 0;
            // Reset buffer/caret
            SceWChar16 dummy[4] = {1, 1, 1, 0};
            sceClibMemset(&caret_rev, 0, sizeof(SceImeCaret));
            caret_rev.index = 1;
            sceImeSetCaret(&caret_rev);
            sceImeSetText(dummy, 4);
            for (int i = 0; i < 4; ++i) output_text[i] = 1;
            forzar_centro = 1;
            return;
        }
    }
    // --- BACKSPACE and NORMAL/SPECIAL KEYS ---
    if (e->id == 1 && (caret == 0 || caret == 1)) {
        wchar_t ch = 0;
        for (int i = 0; i < 4; ++i) {
            if (output_text[i] != 0 && output_text[i] != 1) {
                ch = output_text[i];
                break;
            }
        }
        // BACKSPACE
        if (caret == 0 && (ch == 0x08 || ch == 0x7F || ch == 0)) {
            vita_debug_log("[IME] VK_BACKSPACE (id=1, caretIndex=0, ch=0x%04X '%lc')", ch, ch);
            LiSendKeyboardEvent(0x08, KEY_ACTION_DOWN, 0);
            LiSendKeyboardEvent(0x08, KEY_ACTION_UP, 0);
            for (int i = 0; i < 4; ++i) output_text[i] = 1;
            forzar_centro = 1;
            return;
        }
        // Text mode: any printable character goes as text.
        if (g_keyboard_layout == KB_LAYOUT_TEXT && ch >= 0x20 && ch != 0x7F && (ch < 0xD800 || ch > 0xDFFF)) {
            send_text_char((unsigned)ch);
            for (int i = 0; i < 4; ++i) output_text[i] = 1;
            forzar_centro = 1;
            return;
        }
        // Normal letter key (a-z, A-Z)
        if (caret == 1 && ch && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
            int vk = 0, needs_shift = 0;
            if (find_vk_for_char(ch, &vk, &needs_shift)) {
                vita_debug_log("[IME] LETTER KEY: '%lc' (U+%04X) -> VK: 0x%02X", ch, ch, vk);
                if (needs_shift) LiSendKeyboardEvent(0x10, KEY_ACTION_DOWN, 0);
                LiSendKeyboardEvent(vk, KEY_ACTION_DOWN, 0);
                LiSendKeyboardEvent(vk, KEY_ACTION_UP, 0);
                if (needs_shift) LiSendKeyboardEvent(0x10, KEY_ACTION_UP, 0);
            }
            for (int i = 0; i < 4; ++i) output_text[i] = 1;
            forzar_centro = 1;
            return;
        }
        // SPECIAL KEY (symbols, digits, accents, etc.)
        if (ch && !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
            int vk = 0, needs_shift = 0;
            if (find_vk_for_char(ch, &vk, &needs_shift)) {
                vita_debug_log("[IME] SPECIAL KEY: '%lc' (U+%04X) -> VK: 0x%02X", ch, ch, vk);
                if (needs_shift) LiSendKeyboardEvent(0x10, KEY_ACTION_DOWN, 0);
                LiSendKeyboardEvent(vk, KEY_ACTION_DOWN, 0);
                LiSendKeyboardEvent(vk, KEY_ACTION_UP, 0);
                if (needs_shift) LiSendKeyboardEvent(0x10, KEY_ACTION_UP, 0);
            }
            for (int i = 0; i < 4; ++i) output_text[i] = 1;
            forzar_centro = 1;
            return;
        }
    }
    // --- FLECHA IZQUIERDA ---
    if (e->id == 2 && caret == 0) {
        vita_debug_log("[IME] VK_LEFT (id=2, caretIndex=0)");
        LiSendKeyboardEvent(0x25, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x25, KEY_ACTION_UP, 0);
        for (int i = 0; i < 4; ++i) output_text[i] = 1;
        forzar_centro = 1;
        return;
    }
    // --- FLECHA DERECHA ---
    if (e->id == 2 && caret == 2) {
        vita_debug_log("[IME] VK_RIGHT (id=2, caretIndex=2)");
        LiSendKeyboardEvent(0x27, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x27, KEY_ACTION_UP, 0);
        for (int i = 0; i < 4; ++i) output_text[i] = 1;
        forzar_centro = 1;
        return;
    }
    // --- ENTER (Aceptar) ---
    if (e->id == 5) {
        vita_debug_log("[IME] VK_RETURN (id=5, caretIndex=%d)", caret);
        LiSendKeyboardEvent(0x0D, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x0D, KEY_ACTION_UP, 0);
        for (int i = 0; i < 4; ++i) output_text[i] = 1;
        forzar_centro = 1;
        return;
    }
    // --- CERRAR IME (Minimizar/cancelar) ---
    if (e->id == 4) {
        vita_debug_log("[IME] Close IME (id=4, caretIndex=%d)", caret);
        sceImeClose();
        return;
    }
    // --- RECOVERY: if caretIndex is not 0, 1 or 2 and a valid char came in, reset buffer/caret ---
    if (e->id == 1 && (caret != 0 && caret != 1 && caret != 2)) {
        wchar_t ch = 0;
        for (int i = 0; i < 4; ++i) {
            if (output_text[i] != 0 && output_text[i] != 1) {
                ch = output_text[i];
                break;
            }
        }
        if (ch) {
            forzar_centro = 1;
        }
    }
}

void keyboardsystem_open_keyboard(void) {
    // Mark the overlay open
    _keyboard_overlay_open = true;

    // Sync the global layout with the config before opening the IME
    keyboardsystem_set_layout((KeyboardLayout)config.keyboard_layout);
    vita_debug_log("[IME] keyboard opened");
    // Initialize the IME buffer and caret so the caret can move forever
    ime_working_buffer[0] = 1;
    ime_working_buffer[1] = 1;
    ime_working_buffer[2] = 1;
    ime_working_buffer[3] = 0;
    caret_toggle = 0;
    sceClibMemset(&caret_rev, 0, sizeof(SceImeCaret));
    caret_rev.index = 1;
    logical_caret = 0;
    prev_caret_index = 1;
    ime_just_opened = 1;
    forzar_centro = 0;

    // 1) Load the IME module (if not loaded yet)
    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);

    // 2) Zero the IME parameters and set sdkVersion
    SceImeParam param;
    sceImeParamInit(&param);

    // 3) Fill the mandatory SceImeParam fields
    KeyboardLayout layout = config.keyboard_layout;
    g_keyboard_layout = layout;
    switch (layout) {
        case KB_LAYOUT_EN_US:
            param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
            break;
        case KB_LAYOUT_ES_ES:
            param.supportedLanguages = SCE_IME_LANGUAGE_SPANISH;
            break;
        case KB_LAYOUT_ES_LATAM:
            param.supportedLanguages = SCE_IME_LANGUAGE_SPANISH;
            break;
        case KB_LAYOUT_TEXT:
            // The Vita's own keyboard languages (system settings).
            param.supportedLanguages = 0;
            break;
        default:
            param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
            break;
    }
    param.languagesForced   = layout == KB_LAYOUT_TEXT ? SCE_FALSE : SCE_TRUE;
    param.type              = SCE_IME_TYPE_DEFAULT;
    param.option            = SCE_IME_OPTION_NO_ASSISTANCE;
    param.work              = work_buffer;
    param.handler           = keyboardsystem_ime_event_handler;
    param.initialText       = ime_working_buffer;
    param.maxTextLength     = 4;
    param.inputTextBuffer   = output_text;
    param.enterLabel        = SCE_IME_ENTER_LABEL_DEFAULT;

    // 4) Open the on-screen keyboard
    int res = sceImeOpen(&param);
    if (res < 0) {
        LOGW("input", "on-screen keyboard did not open (0x%08X)", (unsigned)res);
        _keyboard_overlay_open = false;
        return;
    }
    // Only now is it safe to call setText and setCaret
    sceImeSetText(ime_working_buffer, 4);
    sceImeSetCaret(&caret_rev);
    // 5) Update loop: call sceImeUpdate() until it returns < 0
    while (1) {
        if (forzar_centro) {
            SceWChar16 dummy[4] = {1, 1, 1, 0};
            sceImeSetText(dummy, 4);
            ime_working_buffer[0] = 1;
            ime_working_buffer[1] = 1;
            ime_working_buffer[2] = 1;
            ime_working_buffer[3] = 0;
            sceClibMemset(&caret_rev, 0, sizeof(SceImeCaret));
            caret_rev.index = 1;
            sceImeSetCaret(&caret_rev);
            forzar_centro = 0;
        }
        int status = sceImeUpdate();
        if (status < 0) {
            vita_debug_log("[IME] keyboard closed");
            break;
        }
        // 8 ms (125 updates a second) is plenty for typing; this runs on the
        // input thread, on the display thread's core, while the stream goes on.
        sceKernelDelayThread(8000);
    }
    // Mark the overlay closed when leaving the loop
    _keyboard_overlay_open = false;
}
