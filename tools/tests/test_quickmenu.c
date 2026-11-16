/*
 * In-game quick menu: the state machine of src/gui/quickmenu_logic.h (which
 * buttons open it, move, act and close it, and when the game gets its buttons
 * back).
 */
#include "test.h"
#include "quickmenu_logic.h"

#define CONFIRM SCE_CTRL_CROSS
#define CANCEL  SCE_CTRL_CIRCLE
#define COMBO   (SCE_CTRL_SELECT | SCE_CTRL_L1 | SCE_CTRL_R1)

/* One press of a button: down, then up. */
static qm_action press(qm_state *s, uint32_t button) {
  qm_action a = qm_input(s, button, CONFIRM, CANCEL);
  if (a == QM_ACT_NONE)
    a = qm_input(s, 0, CONFIRM, CANCEL);
  else
    qm_input(s, 0, CONFIRM, CANCEL);
  return a;
}

int main(void) {
  TEST_BEGIN("quick menu");
  qm_state s = {0};

  CHECK(qm_combo(COMBO), "L + R + SELECT opens it");
  CHECK(qm_combo(SCE_CTRL_SELECT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER), "L / R read as triggers count too");
  CHECK(qm_combo(COMBO | SCE_CTRL_HEADPHONE), "headphones plugged in do not matter");
  CHECK(!qm_combo(COMBO | SCE_CTRL_START), "with START it is the pause, not the menu");
  CHECK(!qm_combo(SCE_CTRL_SELECT | SCE_CTRL_L1), "L + SELECT alone: nothing");
  CHECK(!qm_blocks(&s, 0), "closed: the game gets its buttons");

  qm_open(&s, COMBO);
  CHECK(s.open && s.selected == QM_RESUME, "opens on Back to the game");
  CHECK(qm_input(&s, COMBO, CONFIRM, CANCEL) == QM_ACT_NONE, "the held combination is not a press");
  CHECK(qm_blocks(&s, COMBO), "open: the game gets nothing");
  CHECK(qm_input(&s, 0, CONFIRM, CANCEL) == QM_ACT_NONE, "releasing the combination: nothing");

  press(&s, SCE_CTRL_DOWN);
  CHECK(s.selected == QM_STATS, "down: statistics");
  CHECK(press(&s, CONFIRM) == QM_ACT_TOGGLE_STATS && s.open, "statistics toggled, the menu stays");
  press(&s, SCE_CTRL_UP);
  press(&s, SCE_CTRL_UP);
  CHECK(s.selected == QM_QUIT, "up from the top wraps to Quit");
  CHECK(press(&s, CONFIRM) == QM_ACT_NONE && s.confirm_quit && s.open, "first press on Quit asks again");
  press(&s, SCE_CTRL_DOWN);
  CHECK(!s.confirm_quit && s.selected == QM_RESUME, "moving away cancels the question");
  press(&s, SCE_CTRL_UP);
  press(&s, CONFIRM);
  CHECK(qm_input(&s, CONFIRM, CONFIRM, CANCEL) == QM_ACT_QUIT && !s.open, "second press quits and closes");
  CHECK(qm_blocks(&s, CONFIRM), "closed with the button still held: the game gets nothing");
  CHECK(qm_blocks(&s, CONFIRM | SCE_CTRL_HEADPHONE), "headphones do not count as a held button");
  CHECK(!qm_blocks(&s, SCE_CTRL_HEADPHONE), "all released: the game gets its buttons again");

  qm_open(&s, COMBO);
  qm_input(&s, 0, CONFIRM, CANCEL);
  CHECK(press(&s, CANCEL) == QM_ACT_CLOSE && !s.open, "cancel goes back to the game");
  CHECK(!qm_blocks(&s, 0), "and the game gets its buttons");

  qm_open(&s, COMBO);
  qm_input(&s, 0, CONFIRM, CANCEL);
  CHECK(press(&s, SCE_CTRL_SELECT) == QM_ACT_CLOSE, "SELECT closes it too");
  qm_blocks(&s, 0);

  qm_open(&s, COMBO);
  qm_input(&s, 0, CONFIRM, CANCEL);
  press(&s, SCE_CTRL_DOWN);
  press(&s, SCE_CTRL_DOWN);
  CHECK(press(&s, CONFIRM) == QM_ACT_KEYBOARD && !s.open, "keyboard closes the menu");
  qm_blocks(&s, 0);
  qm_open(&s, COMBO);
  qm_input(&s, 0, CONFIRM, CANCEL);
  press(&s, SCE_CTRL_UP);
  press(&s, SCE_CTRL_UP);
  CHECK(press(&s, CONFIRM) == QM_ACT_PAUSE && !s.open, "pause closes the menu");

  /* swapped X and O: confirm is Circle */
  qm_blocks(&s, 0);
  qm_open(&s, COMBO);
  qm_input(&s, 0, SCE_CTRL_CIRCLE, SCE_CTRL_CROSS);
  CHECK(qm_input(&s, SCE_CTRL_CIRCLE, SCE_CTRL_CIRCLE, SCE_CTRL_CROSS) == QM_ACT_CLOSE,
        "swapped buttons: Circle confirms Back to the game");

  return TEST_END();
}
