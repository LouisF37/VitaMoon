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

#define BUTTON_DELAY 150 * 1000

static gui_draw_callback gui_global_draw_callback;
static gui_loop_callback gui_global_loop_callback;

vita2d_font *font;

menu_geom make_geom_centered(int w, int h) {
  menu_geom geom = {0};
  geom.x = WIDTH  / 2 - w / 2;
  geom.y = HEIGHT / 2 - h / 2;
  geom.width = w;
  geom.height = h;
  geom.total_y = geom.y + geom.height;
  geom.el = 30;
  return geom;
}

void draw_border(menu_geom geom, unsigned int border_color) {
  vita2d_draw_line(geom.x, geom.y, geom.x+geom.width, geom.y, border_color);
  vita2d_draw_line(geom.x, geom.y, geom.x, geom.y+geom.height, border_color);
  vita2d_draw_line(geom.x+geom.width, geom.y, geom.x+geom.width, geom.y+geom.height, border_color);
  vita2d_draw_line(geom.x, geom.y+geom.height, geom.x+geom.width, geom.y+geom.height, border_color);
}

void draw_text_hcentered(int x, int y, unsigned int color, char *text) {
  int width = vita2d_font_text_width(font, 18, text);
  vita2d_font_draw_text(font, x - width / 2, y, color, 18, text);
}

static int battery_percent;
static bool battery_charging;
static SceRtcTick battery_tick;

void draw_statusbar(menu_geom geom) {
  SceRtcTick current_tick;
  sceRtcGetCurrentTick(&current_tick);
  if (current_tick.tick - battery_tick.tick > 10 * 1000 * 1000) {
    battery_percent = scePowerGetBatteryLifePercent();
    battery_charging = scePowerIsBatteryCharging();

    battery_tick = current_tick;
  }

  SceDateTime time;
  sceRtcGetCurrentClockLocalTime(&time);

  char dt_text[256];
  sprintf(dt_text, "%02d:%02d", time.hour, time.minute);
  int dt_width = vita2d_font_text_width(font, 18, dt_text);
  int battery_width = 30,
      battery_height = 16,
      battery_padding = 2,
      battery_plus_height = 4,
      battery_y_offset = 4,
      battery_charge_width = (float) battery_percent / 100 * battery_width;
  unsigned int battery_color = battery_charging ? 0xff99ffff : (battery_percent < 20 ? 0xff0000ff : 0xff00ff00);

  vita2d_font_draw_text(font, geom.x + geom.width - dt_width - battery_width - 5, geom.y - 5, 0xffffffff, 18, dt_text);

  vita2d_draw_rectangle(
      geom.x + geom.width - battery_width,
      geom.y - battery_height - battery_y_offset,
      battery_width,
      battery_height,
      0xffffffff);

  vita2d_draw_rectangle(
      geom.x + geom.width - battery_width - battery_padding,
      geom.y - battery_y_offset - ((float) battery_height / 2 + (float) battery_plus_height / 2),
      battery_padding,
      battery_plus_height,
      0xffffffff
      );

  vita2d_draw_rectangle(
      geom.x + geom.width - battery_width + battery_padding,
      geom.y - battery_height + battery_padding - battery_y_offset,
      battery_width - battery_padding * 2,
      battery_height - battery_padding * 2,
      0xff000000);

  vita2d_draw_rectangle(
      geom.x + geom.width - battery_charge_width + battery_padding,
      geom.y - battery_height + battery_padding - battery_y_offset,
      battery_charge_width - battery_padding * 2,
      battery_height - battery_padding * 2,
      battery_color);
}

SceTouchData touch_data;
SceCtrlData ctrl_new_pad;

static SceRtcTick button_current_tick, button_until_tick;
bool was_button_pressed(short id) {
  sceRtcGetCurrentTick(&button_current_tick);

  if (ctrl_new_pad.buttons & id) {
    if (sceRtcCompareTick(&button_current_tick, &button_until_tick) > 0) {
      sceRtcTickAddMicroseconds(&button_until_tick, &button_current_tick, BUTTON_DELAY);
      return true;
    }
  }

  return false;
}

bool is_button_down(short id) {
  return ctrl_new_pad.buttons & id;
}

#define lerp(value, from_max, to_max) ((((value*10) * (to_max*10))/(from_max*10))/10)
bool is_rectangle_touched(const SceTouchData *touch, int lx, int ly, int rx, int ry) {
  for (int i = 0; i < touch->reportNum; i++) {
    int x = lerp(touch->report[i].x, 1919, WIDTH);
    int y = lerp(touch->report[i].y, 1087, HEIGHT);
    if (x < lx || x > rx || y < ly || y > ry) continue;
    return true;
  }

  return false;
}

/* Status colours used by the host list (vita2d ABGR values) mapped to the theme. */
static unsigned int theme_status_color(unsigned int c) {
  switch (c) {
    case 0xFF00FF00: return TH_OK;
    case 0xFF0000FF: return TH_ERR;
    case 0xFF00FFFF: return TH_WARN;
    default: return c ? c : TH_TEXT3;
  }
}

/* Menus: glass panel, highlighted row with an accent bar, values on the right,
 * hint row at the bottom. Row geometry (geom.el) and scrolling are unchanged. */
void draw_menu(menu_entry menu[], int total_elements, menu_geom geom, int cursor, int offset) {
  vita2d_font *f = theme_font();
  theme_topbar(NULL);
  theme_glass(geom.x - 14, geom.y - 12, geom.width + 28, geom.height + 20, 14);

  for (int i = 0, cursor_idx = 0; i < total_elements; i++) {
    bool selected = false;
    if (!menu[i].disabled) {
      selected = cursor == cursor_idx;
      cursor_idx++;
    }

    int el_x = geom.x, el_y = geom.y + i * geom.el - offset;
    if (el_y < geom.y - 2 || el_y > geom.total_y - geom.el)
      continue;
    int baseline = el_y + geom.el / 2 + 6;
    const char *name = menu[i].name ? menu[i].name : "";

    if (menu[i].separator) {
      int line_x = el_x + 4;
      if (*name) {
        vita2d_font_draw_text(f, el_x + 4, baseline, TH_ACCENT_HI, 13, name);
        line_x += theme_text_width(f, 13, name) + 10;
      }
      vita2d_draw_rectangle(line_x, baseline - 5, el_x + geom.width - line_x, 1, TH_LINE_FAINT);
      continue;
    }

    if (selected) {
      theme_rounded_rect(el_x - 6, el_y + 1, geom.width + 12, geom.el - 2, 8, TH_FILL_FOCUS);
      theme_rounded_rect(el_x - 6, el_y + 6, 3, geom.el - 12, 1.5f, TH_ACCENT);
    }
    unsigned int text_color = menu[i].disabled ? TH_TEXT3 : (selected ? TH_TEXT : TH_TEXT2);

    int tx = el_x + 6;
    if (menu[i].is_host_entry && *name) {
      vita2d_draw_fill_circle(tx + 5, baseline - 5, 5, theme_status_color(menu[i].color));
      tx += 18;
    }
    int right_x = el_x + geom.width - 4;
    if (menu[i].suffix && *menu[i].suffix) {
      /* suffixes are icons from the icon font */
      int w = vita2d_font_text_width(font, 18, menu[i].suffix);
      right_x -= w;
      vita2d_font_draw_text(font, right_x, baseline + 1, selected ? TH_ACCENT_HI : TH_TEXT3, 18, menu[i].suffix);
      right_x -= 10;
    }
    if (*menu[i].subname) {
      /* next to a label a value takes at most half the row; alone (info
         lines) it may use the whole row */
      char value[256];
      int max_w = *name ? geom.width / 2 : right_x - tx;
      theme_ellipsize(f, 15, menu[i].subname, max_w, value, sizeof(value));
      int w = theme_text_width(f, 15, value);
      vita2d_font_draw_text(f, right_x - w, baseline, selected ? TH_ACCENT_HI : TH_TEXT3, 15, value);
      right_x -= w + 14;
    }
    if (*name) {
      /* translated labels can be longer than the English ones: cut them
         with an ellipsis instead of letting them run under the value */
      char label[256];
      theme_ellipsize(f, 16, name, right_x - tx, label, sizeof(label));
      vita2d_font_draw_text(f, tx, baseline, text_color, 16, label);
    }
  }

  int x = theme_hint(24, TH_HINTS_Y, theme_confirm_glyph(), tr("Select"));
  theme_hint(x, TH_HINTS_Y, theme_cancel_glyph(), tr("Back"));
}

/* Splits text into lines (on spaces and newlines) that fit max_w; returns the count. */
static int wrap_lines(const char *text, vita2d_font *f, unsigned int size, int max_w,
                      char lines[][256], int max_lines) {
  int count = 0;
  char current[256] = "";
  const char *p = text;
  while (*p && count < max_lines) {
    char word[256];
    int n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\n' && n < 255) n++;
    memcpy(word, p, n);
    word[n] = 0;
    p += n;

    char candidate[512];
    snprintf(candidate, sizeof(candidate), "%s%s%s", current, *current ? " " : "", word);
    if (*current && theme_text_width(f, size, candidate) > max_w) {
      snprintf(lines[count++], 256, "%s", current);
      snprintf(current, sizeof(current), "%s", word);
    } else {
      snprintf(current, sizeof(current), "%s", candidate);
    }
    if (*p == '\n' || *p == 0) {
      if (count < max_lines && (*current || *p == '\n'))
        snprintf(lines[count++], 256, "%s", current);
      current[0] = 0;
    }
    if (*p) p++;
  }
  return count;
}

void draw_alert(char *message, menu_geom geom, char *buttons_captions[], int buttons_count) {
  vita2d_font *f = theme_font();
  theme_glass(geom.x, geom.y, geom.width, geom.height, 16);

  char lines[12][256];
  int n = wrap_lines(message, f, 17, geom.width - 48, lines, 12);
  int line_h = 24;
  int area_h = geom.height - (buttons_count ? 56 : 24);
  int y = geom.y + (area_h - n * line_h) / 2 + 18;
  if (y < geom.y + 30) y = geom.y + 30;
  for (int i = 0; i < n; i++) {
    int w = theme_text_width(f, 17, lines[i]);
    vita2d_font_draw_text(f, geom.x + (geom.width - w) / 2, y + i * line_h, TH_TEXT, 17, lines[i]);
  }

  if (buttons_count) {
    const int glyphs[4] = {theme_confirm_glyph(), theme_cancel_glyph(), GLYPH_TRIANGLE, GLYPH_SQUARE};
    const char *defaults[4] = {tr("OK"), tr("Cancel"), tr("Options"), tr("Delete")};
    int total = 0;
    for (int i = 0; i < buttons_count && i < 4; i++) {
      const char *cap = buttons_captions && buttons_captions[i] ? buttons_captions[i] : defaults[i];
      total += 20 + 7 + theme_text_width(f, 15, cap) + 22;
    }
    int x = geom.x + geom.width - 20 - total + 22;
    vita2d_draw_rectangle(geom.x + 16, geom.y + geom.height - 46, geom.width - 32, 1, TH_LINE_FAINT);
    for (int i = 0; i < buttons_count && i < 4; i++) {
      const char *cap = buttons_captions && buttons_captions[i] ? buttons_captions[i] : defaults[i];
      x = theme_hint(x, geom.y + geom.height - 16, glyphs[i], cap);
    }
  }
}

void ui_start() {
  vita2d_start_drawing();
  vita2d_clear_screen();
  theme_draw_background();
}

void ui_end() {
  vita2d_end_drawing();
  vita2d_wait_rendering_done();
  vita2d_swap_buffers();
}

int read_buttons() {
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
        if (hold_times >= 10) {
            btn = curr;
            hold_times = 8;
            btn |= SCE_CTRL_HOLD;
        }
    } else {
        hold_times = 0;
        old = curr;
    }
    return btn;
}

int display_menu(menu_entry menu[], int total_elements, menu_geom *geom_ptr,
                 gui_loop_callback cb, gui_back_callback back_cb,
                 gui_draw_callback draw_callback,
                 void *context) {
  ui_end();

  int offset = 0;
  int cursor = 0;

  menu_geom geom;

  if (!geom_ptr) {
    geom = make_geom_centered(620, 390);
    geom.y = 64;
    geom.total_y = geom.y + geom.height;
  } else {
    geom = *geom_ptr;
  }

  int tick_number = 0;
  int exit_code = 0;

  while (true) {
    int active_elements = 0;
    for (int i = 0; i < total_elements; i++) {
        active_elements += menu[i].disabled ? 0 : 1;
    }

    ui_start();
    tick_number++;

    if (tick_number > 3) {
      if (draw_callback) {
        draw_callback();
      }
      if (gui_global_draw_callback) {
        gui_global_draw_callback();
      }
    }

    draw_menu(menu, total_elements, geom, cursor, offset);

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
    cursor = cursor < 0 ? 0 : cursor;
    cursor = cursor > active_elements - 1 ? active_elements - 1 : cursor;

    int cursor_y = geom.y + ((cursor == 0 ? 0 : real_cursor) * geom.el) - offset;
    offset -= cursor_y < geom.y ? 8 : 0;
    offset -= cursor_y > geom.total_y - geom.el * 2 ? -8 : 0;

    if (cb) {
      exit_code = cb(menu[real_cursor].id, context, &input);
      if (exit_code) {
        goto error;
      }
    }

    if (gui_global_loop_callback) {
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


void display_alert(char *message, char *button_captions[], int buttons_count,
                   gui_loop_callback cb, void *context) {

  menu_geom alert_geom = make_geom_centered(480, 220);

  while (true) {
    ui_start();
    theme_topbar(NULL);

    draw_alert(message, alert_geom, button_captions, buttons_count);

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
          return;
      }
    } else if (result == 0) {
      return;
    }

    ui_end();
  }
}

void display_error(char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  display_alert(buf, NULL, 1, NULL, NULL);
  va_end(opt);
}

void flash_message(char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  va_end(opt);

  ui_end();

  menu_geom alert_geom = make_geom_centered(480, 200);
  ui_start();
  theme_topbar(NULL);
  draw_alert(buf, alert_geom, NULL, 0);

  ui_end();
}

void drw() {
  vita2d_draw_rectangle(0, 0, 150, 150, 0xffffffff);
}

void guilib_init(gui_loop_callback global_loop_cb, gui_draw_callback global_draw_cb) {
  vita2d_init();
  vita2d_set_clear_color(0xff000000);
  font = vita2d_load_font_file("app0:assets/nerdfont.ttf");
  theme_init();

  gui_global_draw_callback = global_draw_cb;
  gui_global_loop_callback = global_loop_cb;
}

int display_confirm(const char* message) {
    char* buttons[] = {tr("Yes"), tr("No")};
    menu_geom geom = make_geom_centered(480, 220);
    while (1) {
        ui_start();
        theme_topbar(NULL);
        draw_alert((char*)message, geom, buttons, 2);
        input_data input = {0};
        input.buttons = read_buttons();
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &input.touch, 1);
        if (input.buttons & config.btn_confirm) {
            ui_end();
            return 1; // X
        } else if (input.buttons & config.btn_cancel) {
            ui_end();
            return 0; // O
        }
        ui_end();
    }
}
