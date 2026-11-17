/*
 * Journeys: what a user does from opening a PC to the end of a game, run
 * through the real menus and the real src/connection.c against the PC of
 * fake_pc.c.
 *
 * A journey gives the answers to the screens in the order they come (library,
 * classic menu, settings, dialogs), what happens meanwhile on the other threads
 * (quick menu, stream lost, stream ended by the PC) and what the Vita must have
 * asked the PC, in order. A step missing, a request that should not have been
 * made, a screen nobody expected, a hang or a stream left open is reported on
 * stderr ("journey <name>: ..."), which tools/ui-preview/run.sh collects.
 */
#include "preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <Limelight.h>
#include "connection.h"
#include "device.h"
#include "errors.h"
#include "gui/quickmenu.h"
#include "gui/ui_connect.h"
#include "gui/ui_home.h"
#include "check_host.h"

#define X   SCE_CTRL_CROSS
#define O   SCE_CTRL_CIRCLE
#define SQ  SCE_CTRL_SQUARE
#define TRI SCE_CTRL_TRIANGLE
#define UP  SCE_CTRL_UP
#define DN  SCE_CTRL_DOWN
#define LT  SCE_CTRL_LEFT
#define RT  SCE_CTRL_RIGHT
#define LTR SCE_CTRL_LTRIGGER
#define RTR SCE_CTRL_RTRIGGER
#define ST  SCE_CTRL_START
#define W   0x80000000u   /* not a button: a second with nothing pressed */

/* ---------------- what happens on the other threads ---------------- */

typedef enum { AT_PLAYING = 1, AT_PAUSED, AT_RECONNECTING, AT_MENUS } bg_when;
typedef enum {
  DO_PAUSE = 1,   /* quick menu: Pause */
  DO_QUIT,        /* quick menu: Quit the game, twice */
  DO_LOSE,        /* moonlight-common-c ends the stream with an error (Wi-Fi drop) */
  DO_PC_ENDS,     /* the PC ends the stream; arg 1: the game was closed on it */
  DO_CANCEL,      /* the user presses O */
  DO_CLOSE_GAME,  /* the game is closed on the PC while no stream runs */
  DO_LOSE_CLOSED, /* the stream is lost and the game closed on the PC meanwhile */
} bg_do;

typedef struct {
  bg_when when;
  unsigned after_ms;   /* in that state for this long */
  bg_do what;
  int arg;
  int refuse_after;    /* the PC then refuses this many launch or resume requests */
} bg_event;

/* ---------------- journeys ---------------- */

#define MAX_ITEMS 24

typedef struct {
  const char *kind;            /* "library", "menu", "settings", "alert", "confirm" */
  unsigned buttons[12];        /* pressed one after the other, 0 ends the list */
} answer;

typedef struct {
  const char *name;
  preview_pc_t pc;             /* the PC when the journey starts */
  bool vita_paired;            /* the Vita's own record of the PC */
  int opens;                   /* times the PC is opened from the home screen */
  answer answers[MAX_ITEMS];
  bg_event events[8];
  const char *expect[MAX_ITEMS];   /* log lines in order; "text*" matches a start */
  bool from_home;              /* starts on the home screen (one paired PC), not by opening the PC */
} journey;

/* The library opens on the game last played from this Vita (preview_main.c
 * gives 1000, 1002 and 1006 a last session, in that order), or on the game
 * running on the PC: 1000 in both cases, 1002 next to it. */
#define PC_IDLE    {.answers = true, .paired = true, .pair_result = GS_OK}
#define PC_RUNNING {.answers = true, .paired = true, .running = 1000, .pair_result = GS_OK}

static const journey k_journeys[] = {
  {"j_play", PC_IDLE, true, 1,
   {{"library", {X}}},
   {{AT_PLAYING, 5000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "stop", "home"}},

  {"j_pause_resume_quit", PC_IDLE, true, 1,
   {{"library", {X}}, {"library", {X}}, {"library", {SQ}}, {"library", {O}}},
   {{AT_PLAYING, 3000, DO_PAUSE}, {AT_PLAYING, 3000, DO_PAUSE}},
   {"open*", "library", "launch 1000", "connect", "library", "library", "stop", "quit 1000", "library",
    "home"}},

  {"j_quickmenu_quit", PC_IDLE, true, 1,
   {{"library", {X}}, {"library", {O}}},
   {{AT_PLAYING, 3000, DO_QUIT}},
   {"open*", "library", "launch 1000", "connect", "stop", "quit 1000", "library", "home"}},

  {"j_lost_reconnect", PC_IDLE, true, 1,
   {{"library", {X}}},
   {{AT_PLAYING, 30000, DO_LOSE, -1}, {AT_PLAYING, 5000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "stop", "resume 1000", "connect", "stop", "home"}},

  {"j_lost_cancel", PC_IDLE, true, 1,
   {{"library", {X}}},
   {{AT_PLAYING, 30000, DO_LOSE, -1}, {AT_RECONNECTING, 300, DO_CANCEL}},
   {"open*", "library", "launch 1000", "connect", "stop", "home"}},

  {"j_lost_gives_up", PC_IDLE, true, 1,
   {{"library", {X}}, {"alert", {X}}},
   {{AT_PLAYING, 30000, DO_LOSE, -1, 1000}},
   {"open*", "library", "launch 1000", "connect", "stop", "resume 1000 refused", "alert: Could not reconnect*",
    "home"}},

  {"j_lost_quickly_three_times", PC_IDLE, true, 1,
   {{"library", {X}}, {"alert", {X}}},
   {{AT_PLAYING, 2000, DO_LOSE, -1}, {AT_PLAYING, 2000, DO_LOSE, -1}, {AT_PLAYING, 2000, DO_LOSE, -1}},
   {"open*", "library", "launch 1000", "connect", "stop", "resume 1000", "connect", "stop", "resume 1000",
    "connect", "stop", "alert: The connection keeps dropping*", "home"}},

  {"j_switch_game", PC_RUNNING, true, 1,
   {{"library", {RT, X}}, {"confirm", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "confirm: *", "quit 1000", "launch 1002", "connect", "stop", "home"}},

  {"j_resume_running_game", PC_RUNNING, true, 1,
   {{"library", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "resume 1000", "connect", "stop", "home"}},

  {"j_classic_menu_launch", PC_IDLE, true, 1,
   {{"library", {TRI}}, {"menu", {DN, X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   /* the classic menu keeps the PC's order: its first game is 1007 */
   {"open*", "library", "menu", "launch 1007", "connect", "stop", "home"}},

  {"j_settings_during_pause_restart", PC_IDLE, true, 1,
   {{"library", {X}}, {"library", {ST}}, {"settings", {RTR, RT, DN, DN, RT, O, O}}, {"library", {X}},
    {"confirm", {X}}},
   {{AT_PLAYING, 3000, DO_PAUSE}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "library", "settings", "library", "confirm: *", "stop",
    "resume 1000", "connect", "stop", "home"}},

  {"j_connect_fails_then_works", {.answers = true, .paired = true, .fail_connect = 1}, true, 1,
   {{"library", {X}}, {"alert", {X}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect failed", "alert: *", "library", "resume 1000", "connect", "stop",
    "home"}},

  {"j_pc_refuses_then_works", {.answers = true, .paired = true, .refuse_start = 1}, true, 1,
   {{"library", {X}}, {"alert", {X}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000 refused", "alert: *", "library", "launch 1000", "connect", "stop", "home"}},

  {"j_back_and_reopen", PC_IDLE, true, 2,
   {{"library", {O}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "home", "open*", "library", "launch 1000", "connect", "stop", "home"}},

  {"j_pc_ends_then_reopen", PC_IDLE, true, 2,
   {{"library", {X}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "stop", "home", "open*", "library", "launch 1000", "connect",
    "stop", "home"}},

  {"j_stream_ends_without_quitting_the_game", PC_IDLE, true, 2,
   {{"library", {X}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 0}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "stop", "home", "open*", "library", "resume 1000", "connect",
    "stop", "home"}},

  {"j_pause_then_pc_ends", PC_IDLE, true, 2,
   {{"library", {X}}, {"library", {}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PAUSE}, {AT_PAUSED, 2000, DO_PC_ENDS, 1}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "library", "stop", "home", "open*", "library", "launch 1000",
    "connect", "stop", "home"}},

  {"j_lost_during_pause", PC_IDLE, true, 2,
   {{"library", {X}}, {"library", {}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PAUSE}, {AT_PAUSED, 2000, DO_LOSE, -1}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "library", "stop", "home", "open*", "library", "resume 1000",
    "connect", "stop", "home"}},

  {"j_pc_forgot_the_vita", {.answers = true, .paired = false, .pair_result = GS_OK}, true, 1,
   {{"menu", {X}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "menu", "pair", "notice: *", "library", "launch 1000", "connect", "stop", "home"}},

  {"j_pairing_cancelled", {.answers = true, .paired = false, .pair_result = GS_CANCELLED}, true, 1,
   {{"menu", {X, O}}},
   {{0}},
   {"open*", "menu", "pair", "home"}},

  {"j_pairing_request_pending_on_the_pc", {.answers = true, .paired = false, .pair_result = GS_ERROR,
                                           .pair_status = 409}, true, 1,
   {{"menu", {X}}, {"alert", {X, O}}},
   {{0}},
   {"open*", "menu", "pair", "alert: A pairing request from this Vita is still waiting*", "home"}},

  {"j_game_closed_on_the_pc_while_in_the_library", PC_RUNNING, true, 1,
   {{"library", {W, W, W, ST}}, {"settings", {O}}, {"library", {X}}},
   {{AT_MENUS, 1000, DO_CLOSE_GAME}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "settings", "library", "launch 1000", "connect", "stop", "home"}},

  {"j_lost_and_game_closed_meanwhile", PC_IDLE, true, 1,
   {{"library", {X}}, {"alert", {X}}},
   {{AT_PLAYING, 30000, DO_LOSE_CLOSED, -1}},
   {"open*", "library", "launch 1000", "connect", "stop", "resume 1000 refused",
    "alert: The game is no longer running on the PC*", "home"}},

  {"j_settings_during_pause_no_restart", PC_IDLE, true, 1,
   {{"library", {X}}, {"library", {ST}}, {"settings", {RTR, RT, DN, DN, RT, O, O}}, {"library", {X}},
    {"confirm", {O}}},
   {{AT_PLAYING, 3000, DO_PAUSE}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "library", "settings", "library", "confirm: *", "stop",
    "home"}},

  {"j_switch_game_declined", PC_RUNNING, true, 1,
   {{"library", {RT, X}}, {"confirm", {O, LT, X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "confirm: *", "resume 1000", "connect", "stop", "home"}},

  {"j_quit_running_game_from_the_library", PC_RUNNING, true, 1,
   {{"library", {SQ}}, {"library", {X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "quit 1000", "library", "launch 1000", "connect", "stop", "home"}},

  {"j_classic_menu_disconnect", PC_IDLE, true, 1,
   {{"library", {TRI}}, {"menu", {X}}},
   {{0}},
   {"open*", "library", "menu", "home"}},

  {"j_classic_menu_resume_paused", PC_IDLE, true, 1,
   {{"library", {X}}, {"library", {TRI}}, {"menu", {X}}},
   {{AT_PLAYING, 3000, DO_PAUSE}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"open*", "library", "launch 1000", "connect", "library", "menu", "stop", "home"}},

  {"j_pc_does_not_answer", {.answers = false}, true, 1,
   {{"alert", {X}}},
   {{0}},
   {"open*", "alert: *", "home"}},

  /* From the home screen (each ends with Quit in its menu). No home.txt at
   * first: its large tile opens the library, which leaves the file when it is
   * left; then the tile starts the game played last at once, without the library. */
  {"j_home_continue", PC_IDLE, true, 0,
   {{"home", {X}}, {"library", {O}}, {"home", {X}}, {"home", {LT, DN, DN, DN, DN, X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"home", "open*", "library", "home", "open*", "launch 1000", "connect", "stop", "home"}, true},

  /* A stream paused, back on the home screen: its large tile goes back into the game. */
  {"j_home_resume_paused", PC_IDLE, true, 0,
   {{"home", {X}}, {"library", {X}}, {"library", {O}}, {"home", {X}}, {"home", {LT, DN, DN, DN, DN, X}}},
   {{AT_PLAYING, 3000, DO_PAUSE}, {AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"home", "open*", "library", "launch 1000", "connect", "library", "home", "stop", "home"}, true},

  /* A game of the strip (the next one played: 1002). */
  {"j_home_strip_game", PC_IDLE, true, 0,
   {{"home", {X}}, {"library", {O}}, {"home", {DN, X}}, {"home", {LT, DN, DN, DN, DN, X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"home", "open*", "library", "home", "open*", "launch 1002", "connect", "stop", "home"}, true},

  /* Another game runs on the PC: the home asks before quitting it. */
  {"j_home_switch_game", PC_RUNNING, true, 0,
   {{"home", {X}}, {"library", {O}}, {"home", {DN, X}}, {"confirm", {X}}, {"home", {LT, DN, DN, DN, DN, X}}},
   {{AT_PLAYING, 3000, DO_PC_ENDS, 1}},
   {"home", "open*", "library", "home", "open*", "confirm: *", "quit 1000", "launch 1002", "connect", "stop",
    "home"}, true},
};
#define JOURNEY_COUNT ((int)(sizeof(k_journeys) / sizeof(k_journeys[0])))

/* ---------------- running one ---------------- */

static const journey *cur = NULL;
static int answer_next = 0;
static int event_next = 0;
static SceUInt64 state_since = 0;   /* when the next event's state began, 0: not in it */

static bool in_state(bg_when when) {
  int status = connection_get_status();
  switch (when) {
    case AT_PLAYING: return status == LI_CONNECTED;
    case AT_PAUSED: return status == LI_MINIMIZED;
    case AT_RECONNECTING: return status != LI_CONNECTED && status != LI_MINIMIZED && connection_was_lost();
    case AT_MENUS: return (status == LI_READY || status == LI_PAIRED) && !connection_was_lost();
  }
  return false;
}

static void quickmenu_keys(const unsigned *keys, int n) {
  quickmenu_reset();
  for (int i = 0; i < n; i++) {
    quickmenu_handle_input(keys[i]);
    quickmenu_handle_input(0);
  }
}

static void fire(const bg_event *e) {
  static const unsigned combo = SCE_CTRL_SELECT | SCE_CTRL_L1 | SCE_CTRL_R1;
  switch (e->what) {
    /* Counted from the bottom of the menu (Pause, then Quit), so that rows
     * added above them do not move these. */
    case DO_PAUSE: {
      const unsigned keys[] = {combo, UP, UP, X};
      quickmenu_keys(keys, 4);
      break;
    }
    case DO_QUIT: {
      const unsigned keys[] = {combo, UP, X, X};
      quickmenu_keys(keys, 4);
      break;
    }
    case DO_LOSE:
      preview_pc.refuse_start = e->refuse_after;
      connection_callbacks.connectionTerminated(e->arg);
      break;
    case DO_PC_ENDS:
      if (e->arg)
        preview_pc.running = 0;
      connection_callbacks.connectionTerminated(ML_ERROR_GRACEFUL_TERMINATION);
      break;
    case DO_CANCEL:
      preview_inject(O);
      break;
    case DO_CLOSE_GAME:
      preview_pc.running = 0;
      break;
    case DO_LOSE_CLOSED:
      preview_pc.running = 0;
      connection_callbacks.connectionTerminated(e->arg);
      break;
  }
}

void preview_background(void) {
  if (!cur || event_next >= 8 || !cur->events[event_next].what)
    return;
  const bg_event *e = &cur->events[event_next];
  if (!in_state(e->when)) {
    state_since = 0;
    return;
  }
  SceUInt64 now = sceKernelGetProcessTimeWide();
  if (state_since == 0)
    state_since = now ? now : 1;
  if (now - state_since < (SceUInt64)e->after_ms * 1000)
    return;
  event_next++;
  state_since = 0;
  fire(e);
}

static void answer_screen(const char *kind) {
  const answer *a = answer_next < MAX_ITEMS ? &cur->answers[answer_next] : NULL;
  if (!a || !a->kind || strcmp(a->kind, kind) != 0) {
    preview_record("unexpected %s", kind);
    return;
  }
  answer_next++;
  for (int i = 0; i < 12 && a->buttons[i]; i++) {
    if (a->buttons[i] == W) {
      for (int f = 0; f < 30; f++)
        preview_inject(0);
    } else {
      preview_inject(a->buttons[i]);
    }
  }
}

void preview_on_screen(const char *name) {
  if (!cur)
    return;
  preview_record("%s", name);
  answer_screen(name);
}

void preview_on_dialog(const char *kind, const char *text) {
  if (!cur)
    return;
  /* the first line, as the expectations quote it */
  char line[80];
  snprintf(line, sizeof(line), "%s", text ? text : "");
  line[strcspn(line, "\n")] = 0;
  preview_record("%s: %s", kind, line);
  if (!strcmp(kind, "alert") || !strcmp(kind, "confirm"))
    answer_screen(kind);
}

static bool line_matches(const char *line, const char *want) {
  size_t n = strlen(want);
  if (n && want[n - 1] == '*')
    return strncmp(line, want, n - 1) == 0;
  return strcmp(line, want) == 0;
}

/* Lines that are a fault unless the journey expects them: what the Vita asks
 * the PC, the screens and questions shown, and the faults noted on the way.
 * Flashed messages and notices are left out. */
static bool line_counts(const char *line) {
  static const char *kinds[] = {"launch", "resume", "quit", "connect", "stop", "pair", "library", "menu",
                                "settings", "home", "alert", "confirm", "unexpected", "stream left open"};
  for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++)
    if (!strncmp(line, kinds[i], strlen(kinds[i])))
      return true;
  return false;
}

static void check(void) {
  if (!cur)
    return;
  if (preview_stream_open() && connection_get_status() != LI_MINIMIZED)
    preview_record("stream left open");
  int n = preview_log_count(), want = 0;
  const char *last = NULL;   /* the line matched last: its repeats are retries */
  char why[512] = "";
  for (int at = 0; at < n && !why[0]; at++) {
    const char *line = preview_log_line(at);
    if (cur->expect[want] && line_matches(line, cur->expect[want])) {
      last = line;
      want++;
    } else if (last && !strcmp(line, last)) {
      /* a retry of the step just seen */
    } else if (line_counts(line)) {
      snprintf(why, sizeof(why), "'%s' where '%s' was expected", line,
               cur->expect[want] ? cur->expect[want] : "(the end)");
    }
  }
  if (!why[0] && want < MAX_ITEMS && cur->expect[want])
    snprintf(why, sizeof(why), "'%s' never came", cur->expect[want]);
  bool ok = !why[0];
  if (getenv("PREVIEW_JOURNEY_LOG") || !ok) {
    fprintf(stderr, "journey %s: %s%s\n  log:", cur->name, ok ? "ok" : "FAILED: ", why);
    for (int i = 0; i < n; i++)
      fprintf(stderr, " %s%s", preview_log_line(i), i + 1 < n ? " |" : "");
    fprintf(stderr, "\n");
  }
}

bool journey_run(const char *name) {
  if (!strcmp(name, "journeys")) {   /* the list, for run.sh */
    for (int i = 0; i < JOURNEY_COUNT; i++)
      printf("%s\n", k_journeys[i].name);
    exit(0);
  }
  for (int i = 0; i < JOURNEY_COUNT; i++) {
    if (strcmp(name, k_journeys[i].name) != 0)
      continue;
    cur = &k_journeys[i];
    preview_pc = cur->pc;
    atexit(check);
    /* The journey ends on its own, in a few thousand frames at most; past this
     * it is waiting on a screen nobody answers, which check() reports. */
    preview_idle_tail(8000);

    if (cur->from_home) {
      /* the Vita knows one PC, seen on by the scan; the library has not left
       * its file for the home yet */
      device_info_t *d = &known_devices.devices[0];
      memset(d, 0, sizeof(*d));
      snprintf(d->name, sizeof(d->name), "MINI-PC");
      snprintf(d->internal, sizeof(d->internal), "192.168.1.24");
      d->port = 47989;
      d->paired = cur->vita_paired;
      known_devices.count = 1;
      g_host_status[0].status = HOST_ONLINE;
      remove(PREVIEW_KEY_DIR "/MINI-PC/home.txt");
      ui_home();   /* ends with the preview (the idle tail) */
      exit(0);
    }

    device_info_t pc;
    memset(&pc, 0, sizeof(pc));
    snprintf(pc.name, sizeof(pc.name), "MINI-PC");
    snprintf(pc.internal, sizeof(pc.internal), "192.168.1.24");
    pc.port = 47989;
    pc.paired = cur->vita_paired;
    for (int open = 0; open < cur->opens; open++) {
      ui_connect_paired_device(&pc);
      preview_record("home");
    }
    exit(0);
  }
  return false;
}
