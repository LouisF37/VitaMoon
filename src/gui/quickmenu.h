#ifndef QUICKMENU_H
#define QUICKMENU_H

/* In-game quick menu, opened with L + R + SELECT during a stream. It is drawn
 * over the live picture by the display thread and driven by the input thread,
 * while the game keeps running on the PC; the game receives no button while it
 * is open. Rows: back to the game, statistics on screen, keyboard, pause
 * (VitaMoon's menus), quit the game. The decisions are in quickmenu_logic.h. */

#include <stdbool.h>
#include <stdint.h>

/* Menu thread, before a stream starts: the PC and the game shown in the
 * menu's header. */
void quickmenu_set_context(const char *host, const char *game);

/* Input thread, once per sample, before anything is sent to the PC: opens the
 * menu on its combination, handles its buttons, and returns true while the
 * game must receive nothing (open, or closed with a button still held). */
bool quickmenu_handle_input(uint32_t buttons);

/* Input thread, when a stream starts: forgets an earlier menu. */
void quickmenu_reset(void);

/* Display thread: draws the menu over the frame when it is open. */
void quickmenu_draw(void);

/* Menu thread, once the stream is paused or over: what was asked from the
 * menu, each returned once. */
bool quickmenu_take_quit_request(void);
bool quickmenu_take_config_changed(void);

#endif // QUICKMENU_H
