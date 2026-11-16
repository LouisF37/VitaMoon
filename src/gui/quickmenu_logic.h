#ifndef QUICKMENU_LOGIC_H
#define QUICKMENU_LOGIC_H

/* The in-game quick menu's state machine: which button does what, kept free
 * of drawing and of Vita calls so that it is tested on a PC
 * (tools/tests/test_quickmenu.c). src/gui/quickmenu.c draws it and carries out
 * the actions. */

#include <stdbool.h>
#include <stdint.h>
#include <psp2/ctrl.h>

/* The rows, top to bottom. */
enum { QM_RESUME, QM_STATS, QM_KEYBOARD, QM_PAUSE, QM_QUIT, QM_COUNT };

/* What a button press asks the menu to do. */
typedef enum {
  QM_ACT_NONE,
  QM_ACT_CLOSE,         /* back to the game */
  QM_ACT_TOGGLE_STATS,  /* statistics on screen, on or off (the menu stays) */
  QM_ACT_KEYBOARD,      /* close, then open the keyboard */
  QM_ACT_PAUSE,         /* close, then pause the stream (Lune's menus) */
  QM_ACT_QUIT,          /* close, then quit the game on the PC (confirmed) */
} qm_action;

typedef struct {
  bool open;
  bool confirm_quit;    /* "Quit the game" pressed once: the next press quits */
  bool wait_release;    /* closed: the game gets nothing until all is released */
  int selected;
  uint32_t prev;        /* buttons of the previous sample, for presses */
} qm_state;

/* The sixteen buttons. The other bits of a sample are states, not presses
 * (headphones plugged in, input taken by the system...): they are ignored. */
#define QM_BUTTONS 0x0000FFFFu

/* L + R + SELECT, without START (START + L + R pauses the stream). The Vita's
 * shoulder buttons read as L1 / R1 with the Ext2 calls of the input thread,
 * as L / R triggers otherwise: either counts. */
static inline bool qm_combo(uint32_t buttons) {
  bool l = buttons & (SCE_CTRL_L1 | SCE_CTRL_LTRIGGER);
  bool r = buttons & (SCE_CTRL_R1 | SCE_CTRL_RTRIGGER);
  return l && r && (buttons & SCE_CTRL_SELECT) && !(buttons & SCE_CTRL_START);
}

/* Opens the menu on its first row; the buttons held now are not presses. */
static inline void qm_open(qm_state *s, uint32_t held) {
  s->open = true;
  s->confirm_quit = false;
  s->wait_release = false;
  s->selected = QM_RESUME;
  s->prev = held & QM_BUTTONS;
}

static inline void qm_close(qm_state *s) {
  s->open = false;
  s->confirm_quit = false;
  s->wait_release = true;
}

/* One sample of the buttons while the menu is open; confirm and cancel are the
 * buttons of the "Swap X & O" setting. Returns the action to carry out. */
static inline qm_action qm_input(qm_state *s, uint32_t buttons, uint32_t confirm, uint32_t cancel) {
  buttons &= QM_BUTTONS;
  uint32_t pressed = buttons & ~s->prev;
  s->prev = buttons;
  if (!s->open || !pressed)
    return QM_ACT_NONE;
  if (pressed & SCE_CTRL_UP) {
    s->selected = (s->selected + QM_COUNT - 1) % QM_COUNT;
    s->confirm_quit = false;
    return QM_ACT_NONE;
  }
  if (pressed & SCE_CTRL_DOWN) {
    s->selected = (s->selected + 1) % QM_COUNT;
    s->confirm_quit = false;
    return QM_ACT_NONE;
  }
  if (pressed & (cancel | SCE_CTRL_SELECT)) {
    qm_close(s);
    return QM_ACT_CLOSE;
  }
  if (!(pressed & confirm))
    return QM_ACT_NONE;
  switch (s->selected) {
    case QM_STATS:
      return QM_ACT_TOGGLE_STATS;
    case QM_KEYBOARD:
      qm_close(s);
      return QM_ACT_KEYBOARD;
    case QM_PAUSE:
      qm_close(s);
      return QM_ACT_PAUSE;
    case QM_QUIT:
      if (!s->confirm_quit) {
        s->confirm_quit = true;
        return QM_ACT_NONE;
      }
      qm_close(s);
      return QM_ACT_QUIT;
    default:
      qm_close(s);
      return QM_ACT_CLOSE;
  }
}

/* True while the game must receive nothing: the menu is open, or it closed and
 * a button is still held (the press that closed it must not reach the game). */
static inline bool qm_blocks(qm_state *s, uint32_t buttons) {
  if (s->open)
    return true;
  if (s->wait_release) {
    if (buttons & QM_BUTTONS)
      return true;
    s->wait_release = false;
  }
  return false;
}

#endif // QUICKMENU_LOGIC_H
