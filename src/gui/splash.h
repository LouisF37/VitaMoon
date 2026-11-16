#ifndef SPLASH_H
#define SPLASH_H

/* Start-up screen: drawn from the first frame vita2d can show, on the theme's
 * sky, while VitaMoon loads. The moon waxes with the loading and becomes the
 * VitaMoon mark once everything is ready; the mark and the name then move to
 * the top bar of Home (timeline: splash_logic.h). */

#include <stdbool.h>

typedef struct {
  const char *name;           /* for the log */
  const char *(*run)(void);   /* NULL when it went well, else why VitaMoon cannot start */
  int weight;                 /* its share of the moon's progress */
} splash_step;

/* Brings vita2d up, shows the screen and loads the menus' fonts while `steps`
 * run one after the other on a thread of their own (they must draw nothing),
 * then ends on Home's top bar; the time each part took is logged. Returns
 * NULL, or the message of the step that failed: the screen is then left for
 * an error message (splash_abort). */
const char *splash_run(const splash_step *steps, int count);

/* Waits until the frames already sent to the screen are shown, so that what
 * is put on the screen next (the error message of main.c) stays there. */
void splash_abort(void);

/* True once, for the first menu after the start-up screen (its opening fades in). */
bool splash_take_intro(void);

#endif // SPLASH_H
