#include <stdbool.h>
#include <vita2d.h>

#include <psp2/touch.h>

#define WIDTH 960
#define HEIGHT 544

#define ICON_LEFT_ARROW   "\xef\x95\x8c"
#define ICON_RIGHT_ARROW  "\xef\x95\x93"
#define ICON_LEFT_RIGHT_ARROWS ICON_LEFT_ARROW ICON_RIGHT_ARROW
#define ICON_NETWORK      "\xef\xa6\x8c"
#define ICON_SPRINTING    "\xeF\xa5\xaD"


typedef struct menu_entry {
  int id;
  unsigned int color;
  char *name, *suffix;
  char subname[1024];
  bool disabled, separator;
  bool is_host_entry; // true only for paired hosts
} menu_entry;

typedef struct menu_geom {
  int x, y, width, height, el, total_y;
  bool page;   /* rows drawn straight on the page instead of in a floating panel */
} menu_geom;

extern vita2d_font *font;

struct menu_geom make_geom_centered(int w, int h);

#define SCE_CTRL_HOLD 0x80000000

typedef struct input_data {
  int buttons;
  SceTouchData touch;
} input_data;

typedef int (*gui_loop_callback) (int, void *, const input_data *);
typedef int (*gui_back_callback) (void *);
typedef void (*gui_draw_callback) (void);

bool is_rectangle_touched(const SceTouchData *touch, int lx, int ly, int rx, int ry);

/* Front-panel tap: true on the frame the finger is lifted without having
 * moved, with the screen position of the tap. */
bool gui_touch_tap(const SceTouchData *touch, int *x, int *y);
/* Ignores the touch in progress (call when a screen opens). */
void gui_touch_reset(void);

/* Frame brackets: ui_start() begins a frame and draws the background,
 * ui_end() presents it. Both do nothing when called twice in a row. */
void ui_start(void);
void ui_end(void);
int read_buttons(void);

#include "theme.h"

/* Options for the next display_menu() call (used once, then cleared). */
typedef struct menu_options {
  const char *title;              /* page title, NULL for none */
  bool has_focus;                 /* start on the entry whose id is focus_id */
  int focus_id;
  bool instant;                   /* no opening animation (menu rebuilt in place) */
  const theme_hint_item *hints;   /* hint row; NULL for "Select / Back" */
  int hint_count;
  /* Optional, replaces hints: fills the hint row for the focused entry and
   * returns the number of hints (the actions depend on the entry). */
  int (*hints_for)(int focus_id, theme_hint_item *out, int max);
} menu_options;

void gui_set_next_options(const menu_options *options);
/* Shorthand: only a title. */
void gui_set_next_title(const char *title);

/* Whether 4x multisampling is in use (chosen when the app starts). */
bool gui_msaa_active(void);

/* Shows the pairing code for one frame (the caller then waits for the PC). */
void flash_pairing_code(const char *host, const char *code, const char *url);

/* Shows a short message for duration_ms, then returns. */
void display_notice(int duration_ms, char *format, ...);

int display_menu(
        menu_entry menu[],
        int total_elements,
        menu_geom *geom_ptr,
        gui_loop_callback loop_callback,
        gui_back_callback back_callback,
        gui_draw_callback draw_callback,
        void *context
        );

void display_alert(
        char *message,
        char *button_captions[],
        int buttons_count,
        gui_loop_callback cb,
        void *context
        );

void display_error(char *format, ...);

void flash_message(char *format, ...);

void guilib_init(gui_loop_callback global_loop_cb, gui_draw_callback global_draw_cb);

// display_confirm, used across the UI
int display_confirm(const char* message);
