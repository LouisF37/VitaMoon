// touchabsolute.c
// The three exclusive touchscreen modes: DS4 touchpad, absolute mouse, tablet.
//
// State is kept per mode so that touchabsolute_reset() can release on the PC
// whatever is still held (a mouse button, a finger) when the stream pauses or
// stops, or when a touch moves into a mapped zone. Taken from
// dubleyu/vita-moonlight-relay (commit 3f9fbb8, "pointer desync"), which also
// made the absolute mode exclusive: it no longer runs the relative gesture
// state machine as well, which sent a second, contradictory mouse stream.

#include "touchabsolute.h"
#include "../config.h"
#include "vita.h"
#include "../debug.h"

#include <Limelight.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool absolute_mouse_enabled = false;
static int last_abs_x = -1;
static int last_abs_y = -1;

static uint8_t ds4_finger_active[10] = {0};
static int ds4_prev_x[10] = {0};
static int ds4_prev_y[10] = {0};

static bool absolute_left_down = false;
static bool absolute_two_finger_active = false;
static bool absolute_two_finger_scroll = false;
static int absolute_two_finger_start_y = 0;
static int absolute_two_finger_last_y = 0;

static bool tablet_mouse_released = false;
static uint8_t tablet_finger_active[10] = {0};

void touchabsolute_enable(bool enable) {
  if (absolute_mouse_enabled != enable) {
    touchabsolute_reset(false);
  }
  absolute_mouse_enabled = enable;
}

bool touchabsolute_is_enabled() {
  return absolute_mouse_enabled;
}

void touchabsolute_reset(bool release_host_state) {
  if (release_host_state) {
    if (absolute_left_down) {
      LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    }

    for (int i = 0; i < 10; ++i) {
      if (ds4_finger_active[i]) {
        LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_UP, i,
                                   0.0f, 0.0f, 0.0f);
      }
      if (tablet_finger_active[i]) {
        LiSendTouchEvent(LI_TOUCH_EVENT_UP, i, 0.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, LI_ROT_UNKNOWN);
      }
    }
  }

  last_abs_x = -1;
  last_abs_y = -1;
  memset(ds4_finger_active, 0, sizeof(ds4_finger_active));
  memset(ds4_prev_x, 0, sizeof(ds4_prev_x));
  memset(ds4_prev_y, 0, sizeof(ds4_prev_y));
  absolute_left_down = false;
  absolute_two_finger_active = false;
  absolute_two_finger_scroll = false;
  absolute_two_finger_start_y = 0;
  absolute_two_finger_last_y = 0;
  tablet_mouse_released = false;
  memset(tablet_finger_active, 0, sizeof(tablet_finger_active));
}

void touchabsolute_handle_ds4(const TouchData *touch, SceRtcTick *current) {
  (void)current;
  const int move_threshold = 1;

  for (int i = 0; i < 10; ++i) {
    int x = (i < touch->finger) ? touch->points[i].x : 0;
    int y = (i < touch->finger) ? touch->points[i].y : 0;
    float norm_x = (float)x / 960.0f;
    float norm_y = (float)y / 544.0f;
    bool active = (x != 0 || y != 0);

    if (active && !ds4_finger_active[i]) {
      LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_DOWN, i,
                                 norm_x, norm_y, 1.0f);
    } else if (active && ds4_finger_active[i]) {
      int dx = abs(x - ds4_prev_x[i]);
      int dy = abs(y - ds4_prev_y[i]);
      if (dx >= move_threshold || dy >= move_threshold) {
        LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_MOVE, i,
                                   norm_x, norm_y, 1.0f);
      }
    } else if (!active && ds4_finger_active[i]) {
      LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_UP, i,
                                 norm_x, norm_y, 0.0f);
    }

    ds4_finger_active[i] = active;
    ds4_prev_x[i] = x;
    ds4_prev_y[i] = y;
  }
}

void touchabsolute_handle_absolute(const TouchData *touch,
                                   SceRtcTick *current,
                                   int *front_state,
                                   short *finger_count,
                                   TouchData *swipe,
                                   TouchData *touch_old) {
  (void)current;
  (void)swipe;

  if (!absolute_mouse_enabled) {
    return;
  }

  if (touch->finger > 0) {
    int x = touch->points[0].x;
    int y = touch->points[0].y;
    if (x != last_abs_x || y != last_abs_y) {
      LiSendMousePositionEvent(x, y, 960, 544);
      last_abs_x = x;
      last_abs_y = y;
    }
  }

  if (touch->finger == 1 && !absolute_left_down) {
    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
    absolute_left_down = true;
  } else if (touch->finger != 1 && absolute_left_down) {
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    absolute_left_down = false;
  }

  const int scroll_threshold = 12;
  if (touch->finger == 2) {
    int average_y = (touch->points[0].y + touch->points[1].y) / 2;
    if (!absolute_two_finger_active) {
      absolute_two_finger_active = true;
      absolute_two_finger_scroll = false;
      absolute_two_finger_start_y = average_y;
      absolute_two_finger_last_y = average_y;
    } else {
      int delta = average_y - absolute_two_finger_last_y;
      if (abs(average_y - absolute_two_finger_start_y) > scroll_threshold) {
        absolute_two_finger_scroll = true;
      }
      if (absolute_two_finger_scroll && delta != 0) {
        LiSendScrollEvent(delta);
      }
      absolute_two_finger_last_y = average_y;
    }
  } else if (absolute_two_finger_active) {
    if (!absolute_two_finger_scroll) {
      LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT);
      LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
    }
    absolute_two_finger_active = false;
    absolute_two_finger_scroll = false;
  }

  // Absolute mode is exclusive. The previous implementation also ran the
  // relative gesture state machine, producing two contradictory mouse streams.
  *front_state = NO_TOUCH_ACTION;
  *finger_count = touch->finger;
  memcpy(touch_old, touch, sizeof(*touch_old));
}

void touchabsolute_handle_tablet(const TouchData *touch) {
  if (!tablet_mouse_released) {
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_MIDDLE);
    LiSendMousePositionEvent(-1, -1, 960, 544);
    tablet_mouse_released = true;
  }

  for (int i = 0; i < 10; ++i) {
    int x = (i < touch->finger) ? touch->points[i].x : 0;
    int y = (i < touch->finger) ? touch->points[i].y : 0;
    float norm_x = (float)x / 960.0f;
    float norm_y = (float)y / 544.0f;
    bool active = (x != 0 || y != 0);

    if (active && !tablet_finger_active[i]) {
      LiSendTouchEvent(LI_TOUCH_EVENT_DOWN, i, norm_x, norm_y, 1.0f,
                       0.0f, 0.0f, LI_ROT_UNKNOWN);
    } else if (active && tablet_finger_active[i]) {
      LiSendTouchEvent(LI_TOUCH_EVENT_MOVE, i, norm_x, norm_y, 1.0f,
                       0.0f, 0.0f, LI_ROT_UNKNOWN);
    } else if (!active && tablet_finger_active[i]) {
      LiSendTouchEvent(LI_TOUCH_EVENT_UP, i, norm_x, norm_y, 0.0f,
                       0.0f, 0.0f, LI_ROT_UNKNOWN);
    }

    tablet_finger_active[i] = active;
  }
}
