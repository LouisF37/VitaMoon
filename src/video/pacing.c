/*
 * Paced display: when to show each frame. See pacing.h.
 */
#include "pacing.h"

void pace_reset(pace_state *p) {
  p->valid = false;
  p->anchor_pts = 0;
  p->anchor_local = 0;
  p->delay_us = PACE_DELAY_START_US;
  p->window_start = 0;
  p->window_min_slack = INT64_MAX;
  p->window_late = 0;
  p->calm_windows = 0;
}

static void anchor_at(pace_state *p, uint64_t pts_us, uint64_t local_us) {
  p->anchor_pts = pts_us;
  p->anchor_local = local_us;
  p->valid = true;
}

uint64_t pace_target(pace_state *p, uint64_t pts_us, uint64_t now_us) {
  if (!p->valid || pts_us < p->anchor_pts) {
    // First frame, or the PC's clock went back (new stream): anchor here.
    anchor_at(p, pts_us, now_us + p->delay_us);
  }
  return p->anchor_local + (pts_us - p->anchor_pts);
}

bool pace_observe(pace_state *p, uint64_t pts_us, uint64_t now_us, uint32_t guard_us) {
  int64_t slack = (int64_t)pace_target(p, pts_us, now_us) - (int64_t)now_us - (int64_t)guard_us;
  if (slack < 0)
    p->window_late++;
  if (slack < p->window_min_slack)
    p->window_min_slack = slack;
  if (p->window_start == 0)
    p->window_start = now_us;
  if (now_us - p->window_start < PACE_WINDOW_US)
    return false;

  if (p->window_late >= 2) {
    p->delay_us = p->delay_us + PACE_DELAY_UP_US > PACE_DELAY_MAX_US ? PACE_DELAY_MAX_US
                                                                     : p->delay_us + PACE_DELAY_UP_US;
    p->calm_windows = 0;
  } else if (p->window_late == 0 && ++p->calm_windows >= 5) {
    p->delay_us = p->delay_us < PACE_DELAY_MIN_US + PACE_DELAY_DOWN_US ? PACE_DELAY_MIN_US
                                                                        : p->delay_us - PACE_DELAY_DOWN_US;
    p->calm_windows = 0;
  }

  // Keep the slowest frame of the window between half the margin and the
  // margin plus 2 ms before the point it must be ready by: below, move the
  // anchor later (back to the margin); above, earlier (back to the margin); in
  // between, leave it, so that due times do not slide back and forth across a
  // screen refresh. At most 4 ms per window.
  int64_t worst = p->window_min_slack;
  int64_t margin = (int64_t)p->delay_us;
  int64_t move = 0;
  if (worst < margin / 2 || worst > margin + PACE_HOLD_US)
    move = margin - worst;   /* positive: later; negative: earlier */
  if (move > PACE_REALIGN_MAX_US) move = PACE_REALIGN_MAX_US;
  if (move < -PACE_REALIGN_MAX_US) move = -PACE_REALIGN_MAX_US;
  p->anchor_local += (uint64_t)move;

  p->window_start = now_us;
  p->window_min_slack = INT64_MAX;
  p->window_late = 0;
  return true;
}

int pace_pick(pace_state *p, const pace_frame *frames, int n, uint64_t deadline, bool catch_up,
              uint32_t *drop) {
  int pick = -1;
  *drop = 0;
  if (catch_up) {
    // A frame was overwritten before it could be shown: show the newest now
    // and anchor the clock on it, rather than pacing a backlog.
    for (int i = 0; i < n; i++) {
      if (frames[i].ready && (pick < 0 || frames[i].seq > frames[pick].seq))
        pick = i;
    }
    if (pick >= 0)
      anchor_at(p, frames[pick].pts_us, deadline);
  } else {
    for (int i = 0; i < n; i++) {
      if (!frames[i].ready)
        continue;
      uint64_t target = pace_target(p, frames[i].pts_us, deadline - p->delay_us);
      if (target > deadline + PACE_JUMP_FUTURE_US || target + PACE_JUMP_PAST_US < deadline) {
        // Far from the clock (pause, PC stall, reconnection): start again here.
        anchor_at(p, frames[i].pts_us, deadline);
        target = deadline;
      }
      if (target <= deadline && (pick < 0 || frames[i].seq > frames[pick].seq))
        pick = i;
    }
  }
  if (pick >= 0) {
    for (int i = 0; i < n && i < 32; i++) {
      if (i != pick && frames[i].ready && frames[i].seq < frames[pick].seq)
        *drop |= 1u << i;
    }
  }
  return pick;
}
