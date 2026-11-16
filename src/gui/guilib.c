#include "guilib.h"
#include "theme.h"

#include "../config.h"
#include "../platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <psp2/net/net.h>
#include <psp2/sysmodule.h>

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>
#include <psp2/power.h>
#include "../i18n.h"

#ifndef ASSET_PREFIX
#define ASSET_PREFIX "app0:assets/"
#endif

static gui_draw_callback gui_global_draw_callback;
static gui_loop_callback gui_global_loop_callback;

/* Icon font (stream indicators). All interface text uses theme_text(). */
vita2d_font *font;

menu_geom make_geom_centered(int w, int h) {
  menu_geom geom = {0};
  geom.x = WIDTH  / 2 - w / 2;
  geom.y = HEIGHT / 2 - h / 2;
  geom.width = w;
  geom.height = h;
  geom.total_y = geom.y + geom.height;
  geom.el = 34;
  return geom;
}

/* ---------------- input helpers ---------------- */

bool is_rectangle_touched(const SceTouchData *touch, int lx, int ly, int rx, int ry) {
  for (int i = 0; i < touch->reportNum; i++) {
    int x = touch->report[i].x * WIDTH / 1920;
    int y = touch->report[i].y * HEIGHT / 1088;
    if (x < lx || x > rx || y < ly || y > ry) continue;
    return true;
  }
  return false;
}

static struct {
  bool down, ignore;
  int x0, y0, x, y;
} tap;

void gui_touch_reset(void) {
  tap.down = false;
  tap.ignore = true;   /* until the finger currently on the screen is lifted */
}

bool gui_touch_tap(const SceTouchData *touch, int *x, int *y) {
  if (touch->reportNum > 0) {
    if (tap.ignore)
      return false;
    int px = touch->report[0].x * WIDTH / 1920;
    int py = touch->report[0].y * HEIGHT / 1088;
    if (!tap.down) {
      tap.down = true;
      tap.x0 = px;
      tap.y0 = py;
    }
    tap.x = px;
    tap.y = py;
    return false;
  }
  tap.ignore = false;
  if (tap.down) {
    tap.down = false;
    if (abs(tap.x - tap.x0) < 24 && abs(tap.y - tap.y0) < 24) {
      *x = tap.x;
      *y = tap.y;
      return true;
    }
  }
  return false;
}

int read_buttons(void) {
    SceCtrlData pad = {0};
    static int old;
    static int hold_times;
    int curr, btn;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceCtrlPeekBufferPositive(0, &pad, 1);

    if (pad.ly < 0x10) {
        pad.buttons |= SCE_CTRL_UP;
    } else if (pad.ly > 0xef) {
        pad.buttons |= SCE_CTRL_DOWN;
    }
    curr = pad.buttons;
    btn = pad.buttons & ~old;
    if (curr && old == curr) {
        hold_times += 1;
        if (hold_times >= 14) {       /* held: repeat every 4 frames */
            btn = curr;
            hold_times = 10;
            btn |= SCE_CTRL_HOLD;
        }
    } else {
        hold_times = 0;
        old = curr;
    }
    return btn;
}

/* ---------------- frames ---------------- */

/* Menus open sub-menus and dialogs from inside their own frame; tracking the
 * state keeps a frame from being presented twice (which showed a stale buffer
 * for one refresh). */
static bool frame_open = false;
static bool msaa_active = false;

/* Whether multisampling is in use (fixed when the graphics start). */
bool gui_msaa_active(void) {
  return msaa_active;
}

void ui_start(void) {
  if (frame_open)
    return;
  frame_open = true;
  vita2d_start_drawing();
  vita2d_clear_screen();
  theme_set_alpha(1.0f);
  theme_draw_background();
}

void ui_end(void) {
  if (!frame_open)
    return;
  frame_open = false;
  vita2d_end_drawing();
  vita2d_wait_rendering_done();
  vita2d_swap_buffers();
}

/* ---------------- menus ---------------- */

/* Status colours used by the host list (vita2d ABGR values) mapped to the theme. */
static unsigned int theme_status_color(unsigned int c) {
  switch (c) {
    case 0xFF00FF00: return TH_OK;
    case 0xFF0000FF: return TH_ERR;
    case 0xFF00FFFF: return TH_WARN;
    default: return c ? c : TH_TEXT3;
  }
}

static bool is_arrows(const char *suffix) {
  return suffix && strcmp(suffix, ICON_LEFT_RIGHT_ARROWS) == 0;
}

typedef struct {
  const char *title;   /* page title, or NULL */
  const theme_hint_item *hints;
  int hint_count;
  int (*hints_for)(int focus_id, theme_hint_item *out, int max);
  bool floating;       /* caller-placed panel instead of a full page */
  float scroll;        /* current scroll offset in pixels */
  float focus_y;       /* animated position of the focus highlight */
  float appear;        /* 0..1 fade-in when the menu opens */
} menu_view;

#define VALUE_ZONE 248   /* width kept on the right for "< value >" */

/*
 * Draws a menu. Page style (default): rows across the content width with thin
 * dividers, label on the left, value on the right ("< value >" for entries that
 * left/right changes), animated focus highlight. Floating style: the same rows
 * inside a glass panel placed by the caller.
 */
static void draw_menu(menu_entry menu[], int total_elements, menu_geom geom, int real_cursor, menu_view *view) {
  theme_topbar(NULL);
  if (view->title)
    theme_header(view->title);

  int content_h = total_elements * geom.el;
  int view_h = geom.total_y - geom.y;
  bool scrollable = content_h > view_h;
  int pad = view->floating ? 12 : 16;
  int size = geom.el >= 40 ? TS_BODY : TS_HINT;
  int value_size = geom.el >= 40 ? TS_HINT : TS_SMALL;

  if (view->floating)
    theme_glass(geom.x - 14, geom.y - 12, geom.width + 28, view_h + 24, 14);

  theme_set_alpha(view->appear);
  if (scrollable) {
    vita2d_set_clip_rectangle(0, geom.y, WIDTH, geom.total_y);
    vita2d_enable_clipping();
  }

  /* focus highlight, behind the rows */
  if (real_cursor >= 0 && real_cursor < total_elements && !menu[real_cursor].disabled)
    theme_focus(geom.x, view->focus_y - view->scroll + 2, geom.width, geom.el - 4);

  for (int i = 0; i < total_elements; i++) {
    float el_y = geom.y + i * geom.el - view->scroll;
    if (el_y + geom.el < geom.y || el_y > geom.total_y)
      continue;
    bool selected = i == real_cursor && !menu[i].disabled;
    float baseline = el_y + geom.el / 2.0f + size * 0.36f;
    const char *name = menu[i].name ? menu[i].name : "";
    int left = geom.x + pad, right = geom.x + geom.width - pad;

    if (menu[i].separator) {
      /* section heading: small accent label and a line */
      int line_x = geom.x;
      if (*name) {
        theme_text(geom.x, el_y + geom.el - 10, TH_ACCENT_HI, TS_SMALL, name);
        line_x += theme_text_width(TS_SMALL, name) + 12;
      }
      theme_rect(line_x, el_y + geom.el - 15, geom.x + geom.width - line_x, 1, TH_DIVIDER);
      continue;
    }

    if (!view->floating && !selected)
      theme_rect(geom.x, el_y + geom.el - 1, geom.width, 1, TH_DIVIDER);

    unsigned int text_color = menu[i].disabled ? TH_TEXT3 : (selected ? TH_TEXT : TH_TEXT2);
    unsigned int value_color = selected ? TH_TEXT : TH_TEXT3;

    if (menu[i].is_host_entry && *name) {
      theme_circle(left + 5, el_y + geom.el / 2.0f, 5, theme_status_color(menu[i].color));
      left += 20;
    }

    if (is_arrows(menu[i].suffix)) {
      /* "< value >" in a fixed zone: nothing moves when the text length changes */
      int zone = geom.width > 520 ? VALUE_ZONE : geom.width / 2;
      float cy = el_y + geom.el / 2.0f;
      unsigned int chev = selected ? TH_TEXT : RGBA8(255, 255, 255, 90);
      theme_chevron(right - zone + 6, cy, 5.5f, -1, chev);
      theme_chevron(right - 6, cy, 5.5f, 1, chev);
      theme_text_fit_center(right - zone / 2.0f, el_y + geom.el / 2.0f + value_size * 0.36f,
                            value_color, value_size, menu[i].subname, zone - 40);
      right -= zone + 12;
    } else {
      if (menu[i].suffix && *menu[i].suffix) {
        theme_text_right(right, el_y + geom.el / 2.0f + TS_SMALL * 0.36f, TH_TEXT3, TS_SMALL, menu[i].suffix);
        right -= theme_text_width(TS_SMALL, menu[i].suffix) + 12;
      }
      if (*menu[i].subname) {
        /* next to a label a value takes at most half the row; alone it may use it all */
        int max_w = *name ? geom.width / 2 : right - left;
        char value[256];
        theme_ellipsize(value_size, menu[i].subname, max_w, value, sizeof(value));
        float vy = el_y + geom.el / 2.0f + value_size * 0.36f;
        if (*name) {
          theme_text_right(right, vy, value_color, value_size, value);
          right -= theme_text_width(value_size, value) + 14;
        } else {
          theme_text(left, vy, TH_TEXT3, value_size, value);
        }
      }
    }
    if (*name)
      theme_text_fit(left, baseline, text_color, size, name, right - left);
  }

  if (scrollable) {
    vita2d_disable_clipping();
    /* position marker on the right edge */
    float track_h = view_h - 8;
    float bar_h = track_h * view_h / content_h;
    float bar_y = geom.y + 4 + (track_h - bar_h) * view->scroll / (content_h - view_h);
    theme_rounded_rect(geom.x + geom.width + 6, bar_y, 3, bar_h, 1.5f, RGBA8(255, 255, 255, 70));
  }
  theme_set_alpha(1.0f);

  if (view->hints_for && real_cursor >= 0 && real_cursor < total_elements) {
    theme_hint_item live[6];
    theme_hints(live, view->hints_for(menu[real_cursor].id, live, 6));
  } else if (view->hints) {
    theme_hints(view->hints, view->hint_count);
  } else {
    const theme_hint_item hints[] = {
      {theme_confirm_glyph(), tr("Select")},
      {theme_cancel_glyph(), tr("Back")},
    };
    theme_hints(hints, 2);
  }
}

static menu_options next_options;
static bool next_options_set = false;

void gui_set_next_options(const menu_options *options) {
  next_options = *options;
  next_options_set = true;
}

void gui_set_next_title(const char *title) {
  menu_options options = {0};
  options.title = title;
  gui_set_next_options(&options);
}

int display_menu(menu_entry menu[], int total_elements, menu_geom *geom_ptr,
                 gui_loop_callback cb, gui_back_callback back_cb,
                 gui_draw_callback draw_callback,
                 void *context) {
  ui_end();
  gui_touch_reset();

  menu_options options = {0};
  if (next_options_set)
    options = next_options;
  next_options_set = false;

  char title[128] = "";
  if (options.title)
    snprintf(title, sizeof(title), "%s", options.title);

  menu_view view = {0};
  view.title = *title ? title : NULL;
  view.hints = options.hints;
  view.hint_count = options.hint_count;
  view.hints_for = options.hints_for;
  view.floating = geom_ptr != NULL && !geom_ptr->page;
  view.appear = options.instant ? 1.0f : 0.0f;

  menu_geom geom;
  if (!geom_ptr) {
    geom.x = TH_MARGIN;
    geom.width = TH_RIGHT - TH_MARGIN;
    geom.y = view.title ? TH_CONTENT_Y : TH_CONTENT_Y0;
    geom.el = 44;
    /* whole rows only, so that nothing is cut at the bottom */
    geom.height = (TH_CONTENT_END - geom.y) / geom.el * geom.el;
    geom.total_y = geom.y + geom.height;
  } else {
    geom = *geom_ptr;
  }

  int cursor = 0;            /* index among the enabled entries */
  if (options.has_focus) {
    int c = 0;
    for (int i = 0; i < total_elements; i++) {
      if (menu[i].disabled)
        continue;
      if (menu[i].id == options.focus_id) {
        cursor = c;
        break;
      }
      c++;
    }
  }
  float target_scroll = 0;
  bool first_frame = true;
  int exit_code = 0;

  while (true) {
    int active_elements = 0;
    for (int i = 0; i < total_elements; i++) {
        active_elements += menu[i].disabled ? 0 : 1;
    }
    cursor = cursor > active_elements - 1 ? active_elements - 1 : cursor;
    cursor = cursor < 0 ? 0 : cursor;

    int real_cursor = 0;
    for (int c = 0; real_cursor < total_elements; real_cursor++) {
      if (menu[real_cursor].disabled) {
        continue;
      }
      if (cursor == c) {
        break;
      }
      c++;
    }
    if (real_cursor >= total_elements)
      real_cursor = total_elements - 1;

    /* Scroll so that the focused row and one row around it stay visible. */
    int view_h = geom.total_y - geom.y;
    int content_h = total_elements * geom.el;
    float row_top = real_cursor * geom.el;
    if (cursor == 0)
      target_scroll = 0;                       /* show any heading above the first entry */
    else if (row_top - geom.el < target_scroll)
      target_scroll = row_top - geom.el;
    else if (row_top + 2 * geom.el > target_scroll + view_h)
      target_scroll = row_top + 2 * geom.el - view_h;
    if (target_scroll > content_h - view_h) target_scroll = content_h - view_h;
    if (target_scroll < 0) target_scroll = 0;

    if (first_frame) {
      view.scroll = target_scroll;
      view.focus_y = geom.y + row_top;
      first_frame = false;
    } else {
      view.scroll = theme_approach(view.scroll, target_scroll);
      view.focus_y = theme_approach(view.focus_y, geom.y + row_top);
    }
    view.appear = view.appear + 0.2f > 1.0f ? 1.0f : view.appear + 0.2f;

    ui_start();
    if (draw_callback) {
      draw_callback();
    }
    if (gui_global_draw_callback) {
      gui_global_draw_callback();
    }

    draw_menu(menu, total_elements, geom, total_elements ? real_cursor : -1, &view);

    // select item
    input_data input = {0};
    input.buttons = read_buttons();
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &input.touch, 1);
    if (input.buttons & SCE_CTRL_DOWN) {
      cursor += 1;
    }
    if (input.buttons & SCE_CTRL_UP) {
      cursor -= 1;
    }

    /* Touch: a tap on a row focuses it; a tap on the focused row confirms. */
    int tx, ty;
    if (gui_touch_tap(&input.touch, &tx, &ty) &&
        tx >= geom.x && tx <= geom.x + geom.width && ty >= geom.y && ty < geom.total_y) {
      int row = (int)((ty - geom.y + view.scroll) / geom.el);
      if (row >= 0 && row < total_elements && !menu[row].disabled) {
        if (row == real_cursor) {
          input.buttons |= config.btn_confirm;
        } else {
          int c = 0;
          for (int i = 0; i < row; i++)
            c += menu[i].disabled ? 0 : 1;
          cursor = c;
        }
      }
    }
    cursor = cursor < 0 ? 0 : cursor;
    cursor = cursor > active_elements - 1 ? active_elements - 1 : cursor;

    if (cb && total_elements) {
      exit_code = cb(menu[real_cursor].id, context, &input);
      if (exit_code) {
        goto error;
      }
    }

    if (gui_global_loop_callback && total_elements) {
      gui_global_loop_callback(menu[real_cursor].id, context, &input);
    }

    if (input.buttons & config.btn_cancel && (input.buttons & SCE_CTRL_HOLD) == 0) {
      if (!back_cb || back_cb(context) == 0) {
        exit_code = 1;
        goto error;
      }
    }

    ui_end();
  }

  return 0;

error:
  ui_end();
  return exit_code;
}

/* ---------------- dialogs ---------------- */

#define DIALOG_W 540
#define DIALOG_LINE_H 26
#define DIALOG_MAX_LINES 12

/* A centred panel sized to its text: message lines, then the button hints. */
static void draw_dialog_hints(const char *message, const theme_hint_item *hints, int hints_count) {
  char lines[DIALOG_MAX_LINES][256];
  int n = theme_wrap(TS_BODY, message, DIALOG_W - 72, lines, DIALOG_MAX_LINES);
  int text_h = n * DIALOG_LINE_H;
  int h = 44 + text_h + (hints_count ? 64 : 36);
  if (h < 132) h = 132;
  int x = (WIDTH - DIALOG_W) / 2, y = (HEIGHT - h) / 2;

  theme_rect(0, 0, WIDTH, HEIGHT, RGBA8(2, 5, 14, 110));   /* dim what is behind */
  theme_glass(x, y, DIALOG_W, h, 16);

  int area_h = h - (hints_count ? 60 : 0);
  float ty = y + (area_h - text_h) / 2.0f + DIALOG_LINE_H * 0.72f;
  for (int i = 0; i < n; i++)
    theme_text_center(WIDTH / 2.0f, ty + i * DIALOG_LINE_H, TH_TEXT, TS_BODY, lines[i]);

  if (hints_count) {
    theme_rect(x + 24, y + h - 60, DIALOG_W - 48, 1, TH_DIVIDER);
    theme_hints_at(hints, hints_count, WIDTH / 2.0f, y + h - 24);
  }
}

/* Buttons in order: confirm, cancel, triangle, square. */
static void draw_dialog(const char *message, char *buttons_captions[], int buttons_count) {
  const int glyphs[4] = {theme_confirm_glyph(), theme_cancel_glyph(), GLYPH_TRIANGLE, GLYPH_SQUARE};
  const char *defaults[4] = {tr("OK"), tr("Cancel"), tr("Options"), tr("Delete")};
  theme_hint_item hints[4];
  int count = buttons_count > 4 ? 4 : buttons_count;
  for (int i = 0; i < count; i++) {
    hints[i].glyph = glyphs[i];
    hints[i].label = buttons_captions && buttons_captions[i] ? buttons_captions[i] : defaults[i];
  }
  draw_dialog_hints(message, hints, count < 0 ? 0 : count);
}

void display_alert(char *message, char *button_captions[], int buttons_count,
                   gui_loop_callback cb, void *context) {
  ui_end();
  gui_touch_reset();

  while (true) {
    ui_start();
    theme_topbar(NULL);
    draw_dialog(message, button_captions, buttons_count);

    input_data input = {0};
    input.buttons = read_buttons();
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &input.touch, 1);

    int result = -1;

    if (input.buttons & SCE_CTRL_HOLD) {
      ui_end();
      continue;
    }

    if (input.buttons & config.btn_confirm) {
      result = 0;
    } else if (input.buttons & config.btn_cancel) {
      result = 1;
    } else if (input.buttons & SCE_CTRL_TRIANGLE) {
      result = 2;
    } else if (input.buttons & SCE_CTRL_SQUARE) {
      result = 3;
    }

    if (cb && result != -1 && result < buttons_count) {
      switch(cb(result, context, &input)) {
        case 1:
          ui_end();
          return;
      }
    } else if (result == 0) {
      ui_end();
      return;
    }

    ui_end();
  }
}

void display_notice(int duration_ms, char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  va_end(opt);

  ui_end();
  /* one frame per refresh (ui_end waits for the screen), so ~60 frames a second */
  int frames = duration_ms * 60 / 1000;
  for (int i = 0; i < frames; i++) {
    ui_start();
    theme_topbar(NULL);
    draw_dialog(buf, NULL, 0);
    ui_end();
    if (i > 12 && (read_buttons() & (config.btn_confirm | config.btn_cancel)))
      break;   /* any key dismisses it early */
  }
  gui_touch_reset();
}

void display_error(char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  va_end(opt);
  display_alert(buf, NULL, 1, NULL, NULL);
}

/* Shows a message for one frame and returns: used before a blocking operation
 * ("Connecting...") so the screen is not left on the previous menu. */
void flash_message(char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  va_end(opt);

  ui_end();
  ui_start();
  theme_topbar(NULL);
  draw_dialog(buf, NULL, 0);
  ui_end();
}

/* One frame of a message with a single "Cancel" hint, for a wait the caller
 * runs itself (it reads the buttons between frames). */
void flash_cancelable(const char *message) {
  theme_hint_item cancel = {theme_cancel_glyph(), tr("Cancel")};
  ui_end();
  ui_start();
  theme_topbar(NULL);
  draw_dialog_hints(message, &cancel, 1);
  ui_end();
}

/* Pairing screen, drawn once: the code in large digits and where to type it.
 * The caller then blocks in gs_pair() until the PC has accepted the code. */
void flash_pairing_code(const char *host, const char *code, const char *url) {
  char title[160], lines[3][256];
  snprintf(title, sizeof(title), tr("Pairing with %s"), host);
  const char *how = tr("On the PC, open the Sunshine web page, choose PIN and type this code.");
  int n = theme_wrap(TS_HINT, how, DIALOG_W - 72, lines, 3);
  if (n > 1) {
    /* lines of similar length rather than a last line of one word */
    int balanced = theme_text_width(TS_HINT, how) / n + 40;
    if (balanced < DIALOG_W - 72)
      n = theme_wrap(TS_HINT, how, balanced, lines, 3);
  }

  const int box_w = 56, box_h = 68, gap = 14;
  int digits = (int)strlen(code);
  int row_w = digits * box_w + (digits - 1) * gap;
  int h = 64 + box_h + 30 + n * 22 + 34 + 40;
  int x = (WIDTH - DIALOG_W) / 2, y = (HEIGHT - h) / 2;

  ui_end();
  ui_start();
  theme_topbar(NULL);
  theme_rect(0, 0, WIDTH, HEIGHT, RGBA8(2, 5, 14, 110));
  theme_glass(x, y, DIALOG_W, h, 16);
  theme_text_fit_center(WIDTH / 2.0f, y + 42, TH_TEXT, TS_LEAD, title, DIALOG_W - 72);

  float bx = (WIDTH - row_w) / 2.0f, by = y + 64;
  for (int i = 0; i < digits; i++) {
    char digit[2] = {code[i], 0};
    theme_rounded_rect(bx, by, box_w, box_h, 10, TH_FILL_SOFT);
    theme_rounded_outline(bx, by, box_w, box_h, 10, TH_ACCENT);
    theme_text_center(bx + box_w / 2.0f, by + box_h / 2.0f + TS_HERO * 0.36f, TH_TEXT, TS_HERO, digit);
    bx += box_w + gap;
  }

  float ty = by + box_h + 36;
  for (int i = 0; i < n; i++)
    theme_text_center(WIDTH / 2.0f, ty + i * 22, TH_TEXT2, TS_HINT, lines[i]);
  ty += n * 22 + 6;
  theme_text_fit_center(WIDTH / 2.0f, ty, TH_ACCENT, TS_HINT, url, DIALOG_W - 72);
  theme_text_center(WIDTH / 2.0f, y + h - 22, TH_TEXT3, TS_SMALL, tr("Waiting for the PC..."));
  ui_end();
}

int display_confirm(const char* message) {
    char* buttons[] = {tr("Yes"), tr("No")};
    ui_end();
    gui_touch_reset();
    while (1) {
        ui_start();
        theme_topbar(NULL);
        draw_dialog(message, buttons, 2);
        int pressed = read_buttons();
        ui_end();
        if (pressed & SCE_CTRL_HOLD)
            continue;
        if (pressed & config.btn_confirm)
            return 1;
        if (pressed & config.btn_cancel)
            return 0;
    }
}

void guilib_init(gui_loop_callback global_loop_cb, gui_draw_callback global_draw_cb) {
  /* 4x multisampling smooths rounded shapes and diagonal strokes (VitaRPS5
   * initialises vita2d the same way). It also applies to the stream, which is
   * drawn with the same context, hence the setting to turn it off. */
  msaa_active = config.ui_msaa;
  vita2d_init_advanced_with_msaa(1 * 1024 * 1024,
                                 msaa_active ? SCE_GXM_MULTISAMPLE_4X : SCE_GXM_MULTISAMPLE_NONE);
  vita2d_set_clear_color(0xff000000);
  font = vita2d_load_font_file(ASSET_PREFIX "nerdfont.ttf");
  theme_init();

  gui_global_draw_callback = global_draw_cb;
  gui_global_loop_callback = global_loop_cb;
}
