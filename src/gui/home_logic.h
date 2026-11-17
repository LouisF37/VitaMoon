#ifndef HOME_LOGIC_H
#define HOME_LOGIC_H

/* Home screen: where the cursor goes, and the small file that lets the home
 * show a PC's games without asking the PC. Kept free of Vita calls: tested on
 * a PC (tools/tests/test_home.c). The drawing is in ui_home.c.
 *
 * The home has a side menu (Home, Library, Computers, Settings, Quit) and, on
 * its right, a preview of the entry under the cursor. On Home the preview is a
 * dashboard of tiles that the cursor can enter: the game to pick up (a large
 * tile), the PC, then a strip of other games and "See all".
 *
 * The games come from <data folder>/<PC name>/home.txt, written by the library
 * when it is left (the library has just read the PC's list, the favourites and
 * the last sessions): the home reads one small file per PC and nothing else. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- the cursor ---------------- */

enum { HOME_MENU_HOME, HOME_MENU_LIBRARY, HOME_MENU_COMPUTERS, HOME_MENU_SETTINGS, HOME_MENU_QUIT, HOME_MENU_COUNT };

/* Tiles of the dashboard that the cursor stops on. Two rows: the game to pick
 * up and the PC; then the strip of three games and "See all". */
enum {
  HOME_TILE_RESUME, HOME_TILE_PC,
  HOME_TILE_GAME0, HOME_TILE_GAME1, HOME_TILE_GAME2, HOME_TILE_ALL,
  HOME_TILE_COUNT
};
#define HOME_TILE_BIT(t) (1u << (t))

typedef struct {
  bool in_tiles;   /* on the tiles (the menu then shows Home as the current entry) */
  int menu;        /* HOME_MENU_* under the cursor, or HOME_MENU_HOME while in the tiles */
  int tile;        /* HOME_TILE_*, while in the tiles */
} home_nav;

typedef enum { HOME_KEY_UP, HOME_KEY_DOWN, HOME_KEY_LEFT, HOME_KEY_RIGHT, HOME_KEY_CONFIRM, HOME_KEY_BACK } home_key;

typedef enum {
  HOME_ACT_NONE,
  HOME_ACT_LIBRARY,     /* open the library of the PC shown */
  HOME_ACT_COMPUTERS,   /* open the Computers page */
  HOME_ACT_SETTINGS,
  HOME_ACT_QUIT,
  HOME_ACT_TILE         /* act on nav->tile */
} home_action;

static inline bool home_tile_on(unsigned tiles, int t) {
  return t >= 0 && t < HOME_TILE_COUNT && (tiles & HOME_TILE_BIT(t)) != 0;
}

/* First tile on, from `from` towards `to` (inclusive, either way), or -1. */
static inline int home_tile_scan(unsigned tiles, int from, int to) {
  int step = from <= to ? 1 : -1;
  for (int t = from;; t += step) {
    if (home_tile_on(tiles, t))
      return t;
    if (t == to)
      return -1;
  }
}

/* The tile to start on: the game to pick up, else the first one on. */
static inline int home_first_tile(unsigned tiles) {
  return home_tile_scan(tiles, HOME_TILE_RESUME, HOME_TILE_COUNT - 1);
}

/* After the tiles changed (data reloaded): keeps the cursor on a tile that is on. */
static inline void home_nav_fix(home_nav *nav, unsigned tiles) {
  if (nav->menu < 0 || nav->menu >= HOME_MENU_COUNT)
    nav->menu = HOME_MENU_HOME;
  if (!nav->in_tiles)
    return;
  nav->menu = HOME_MENU_HOME;
  if (!home_tile_on(tiles, nav->tile)) {
    int t = home_first_tile(tiles);
    if (t < 0)
      nav->in_tiles = false;
    else
      nav->tile = t;
  }
}

/* Moves the cursor for one key; returns what to do. `tiles`: the tiles on. */
static inline home_action home_nav_key(home_nav *nav, home_key key, unsigned tiles) {
  if (!nav->in_tiles) {
    switch (key) {
      case HOME_KEY_UP:
        if (nav->menu > 0) nav->menu--;
        return HOME_ACT_NONE;
      case HOME_KEY_DOWN:
        if (nav->menu < HOME_MENU_COUNT - 1) nav->menu++;
        return HOME_ACT_NONE;
      case HOME_KEY_RIGHT:
      case HOME_KEY_CONFIRM:
        if (nav->menu == HOME_MENU_HOME) {
          int t = home_first_tile(tiles);
          if (t >= 0) {
            nav->in_tiles = true;
            nav->tile = t;
          }
          return HOME_ACT_NONE;
        }
        if (key == HOME_KEY_RIGHT)
          return HOME_ACT_NONE;
        switch (nav->menu) {
          case HOME_MENU_LIBRARY: return HOME_ACT_LIBRARY;
          case HOME_MENU_COMPUTERS: return HOME_ACT_COMPUTERS;
          case HOME_MENU_SETTINGS: return HOME_ACT_SETTINGS;
          default: return HOME_ACT_QUIT;
        }
      case HOME_KEY_BACK:
        nav->menu = HOME_MENU_HOME;   /* the shortcut back to Home */
        return HOME_ACT_NONE;
      default:
        return HOME_ACT_NONE;
    }
  }

  bool top = nav->tile <= HOME_TILE_PC;
  int t = -1;
  switch (key) {
    case HOME_KEY_LEFT:
      t = nav->tile == HOME_TILE_RESUME || nav->tile == HOME_TILE_GAME0 ? -1
          : home_tile_scan(tiles, nav->tile - 1, top ? HOME_TILE_RESUME : HOME_TILE_GAME0);
      if (t < 0) {
        nav->in_tiles = false;   /* past the left edge: back to the menu */
        nav->menu = HOME_MENU_HOME;
        return HOME_ACT_NONE;
      }
      break;
    case HOME_KEY_RIGHT:
      t = nav->tile == HOME_TILE_PC || nav->tile == HOME_TILE_ALL ? -1
          : home_tile_scan(tiles, nav->tile + 1, top ? HOME_TILE_PC : HOME_TILE_ALL);
      break;
    case HOME_KEY_DOWN:
      if (top)
        t = nav->tile == HOME_TILE_RESUME ? home_tile_scan(tiles, HOME_TILE_GAME0, HOME_TILE_ALL)
                                          : home_tile_scan(tiles, HOME_TILE_ALL, HOME_TILE_GAME0);
      break;
    case HOME_KEY_UP:
      if (!top)
        t = nav->tile <= HOME_TILE_GAME1 ? home_tile_scan(tiles, HOME_TILE_RESUME, HOME_TILE_PC)
                                         : home_tile_scan(tiles, HOME_TILE_PC, HOME_TILE_RESUME);
      break;
    case HOME_KEY_CONFIRM:
      return HOME_ACT_TILE;
    case HOME_KEY_BACK:
      nav->in_tiles = false;
      nav->menu = HOME_MENU_HOME;
      return HOME_ACT_NONE;
  }
  if (t >= 0)
    nav->tile = t;
  return HOME_ACT_NONE;
}

/* ---------------- a PC's games, as the library left them ---------------- */

#define HOME_SHELF_MAX 12   /* the library preview shows two rows of six */
#define HOME_RECENT_MAX 4   /* the game to pick up and three more */

typedef struct {
  int id;
  bool favourite;
  char when[20];   /* last session, "YYYY-MM-DD HH:MM" as in stats/last_<id>.txt; "" if none */
  char name[96];
} home_game;

typedef struct {
  char covers[256];    /* folder of the PC's covers (boxart.c) */
  char opened[20];     /* when the library of this PC was last left, "YYYY-MM-DD HH:MM" */
  int total;           /* games on the PC */
  int shelf_count;
  home_game shelf[HOME_SHELF_MAX];     /* the start of the library, in its order */
  int recent_count;
  home_game recent[HOME_RECENT_MAX];   /* the games played last, the latest first */
} home_games;

#define HOME_FILE_HEADER "vitamoon-home 1"

/* Copies text, turning tabs and line breaks into spaces: the file is one record per line. */
static inline void home_copy_field(char *out, size_t n, const char *text) {
  size_t i = 0;
  for (; text && text[i] && i + 1 < n; i++)
    out[i] = text[i] == '\t' || text[i] == '\n' || text[i] == '\r' ? ' ' : text[i];
  if (n)
    out[i < n ? i : n - 1] = 0;
}

/* Adds a game played at `when` to the recent list (kept latest first, at most
 * HOME_RECENT_MAX). Games without a session are left out. */
static inline void home_add_recent(home_games *g, int id, const char *when, const char *name) {
  if (!when || !when[0])
    return;
  int at = g->recent_count;
  while (at > 0 && strcmp(g->recent[at - 1].when, when) < 0)
    at--;
  if (at >= HOME_RECENT_MAX)
    return;
  int last = g->recent_count < HOME_RECENT_MAX ? g->recent_count : HOME_RECENT_MAX - 1;
  for (int i = last; i > at; i--)
    g->recent[i] = g->recent[i - 1];
  home_game *r = &g->recent[at];
  memset(r, 0, sizeof(*r));
  r->id = id;
  home_copy_field(r->when, sizeof(r->when), when);
  home_copy_field(r->name, sizeof(r->name), name);
  if (g->recent_count < HOME_RECENT_MAX)
    g->recent_count++;
}

/* Text of the file; returns its length, or -1 if `n` is too small. */
static inline int home_games_format(const home_games *g, char *out, size_t n) {
  size_t len = 0;
#define HOME_PUT(...)                                                   \
  do {                                                                  \
    int w = snprintf(out + len, n - len, __VA_ARGS__);                  \
    if (w < 0 || (size_t)w >= n - len)                                  \
      return -1;                                                        \
    len += (size_t)w;                                                   \
  } while (0)
  if (!n)
    return -1;
  out[0] = 0;
  HOME_PUT("%s\ncovers\t%s\nopened\t%s\ntotal\t%d\n", HOME_FILE_HEADER, g->covers, g->opened, g->total);
  for (int i = 0; i < g->shelf_count && i < HOME_SHELF_MAX; i++)
    HOME_PUT("shelf\t%d\t%d\t%s\t%s\n", g->shelf[i].id, g->shelf[i].favourite ? 1 : 0,
             g->shelf[i].when[0] ? g->shelf[i].when : "-", g->shelf[i].name);
  for (int i = 0; i < g->recent_count && i < HOME_RECENT_MAX; i++)
    HOME_PUT("recent\t%d\t%s\t%s\n", g->recent[i].id, g->recent[i].when, g->recent[i].name);
#undef HOME_PUT
  return (int)len;
}

/* A number written in the file (0 if it is not one). */
static inline int home_int(const char *text) {
  return text ? (int)strtol(text, NULL, 10) : 0;
}

/* Next tab-separated field of *p (which moves past it); NULL when there is none. */
static inline char *home_field(char **p) {
  if (!*p)
    return NULL;
  char *start = *p;
  char *tab = strchr(start, '\t');
  if (tab) {
    *tab = 0;
    *p = tab + 1;
  } else {
    *p = NULL;
  }
  return start;
}

/* Reads the file's text (changed in place). False if it is not such a file. */
static inline bool home_games_parse(char *text, home_games *g) {
  memset(g, 0, sizeof(*g));
  char *line = text;
  bool header = false;
  while (line && *line) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = 0;
    size_t l = strlen(line);
    if (l && line[l - 1] == '\r')
      line[l - 1] = 0;
    if (!header) {
      if (strcmp(line, HOME_FILE_HEADER) != 0)
        return false;
      header = true;
      line = next;
      continue;
    }
    char *p = line;
    char *kind = home_field(&p);
    if (!strcmp(kind, "covers") && p) {
      home_copy_field(g->covers, sizeof(g->covers), p);
    } else if (!strcmp(kind, "opened") && p) {
      home_copy_field(g->opened, sizeof(g->opened), p);
    } else if (!strcmp(kind, "total") && p) {
      g->total = home_int(p);
    } else if (!strcmp(kind, "shelf") && g->shelf_count < HOME_SHELF_MAX) {
      char *id = home_field(&p), *fav = home_field(&p), *when = home_field(&p), *name = p;
      if (id && fav && when && name) {
        home_game *s = &g->shelf[g->shelf_count++];
        s->id = home_int(id);
        s->favourite = fav[0] == '1';
        home_copy_field(s->when, sizeof(s->when), strcmp(when, "-") ? when : "");
        home_copy_field(s->name, sizeof(s->name), name);
      }
    } else if (!strcmp(kind, "recent") && g->recent_count < HOME_RECENT_MAX) {
      char *id = home_field(&p), *when = home_field(&p), *name = p;
      if (id && when && name) {
        home_game *r = &g->recent[g->recent_count++];
        r->id = home_int(id);
        home_copy_field(r->when, sizeof(r->when), when);
        home_copy_field(r->name, sizeof(r->name), name);
      }
    }
    line = next;
  }
  return header;
}

/* The PC the dashboard shows: the one whose library was left last (`opened`
 * of each PC, "" when it has no file). -1 when none has one. */
static inline int home_pick_host(const char *const opened[], int count) {
  int best = -1;
  for (int i = 0; i < count; i++) {
    if (!opened[i] || !opened[i][0])
      continue;
    if (best < 0 || strcmp(opened[i], opened[best]) > 0)
      best = i;
  }
  return best;
}

/* The games of the strip under the game to pick up: the other recent ones,
 * then the start of the library, never one twice. Returns how many (<= max). */
static inline int home_strip_games(const home_games *g, int resume_id, const home_game *out[], int max) {
  int n = 0;
  for (int pass = 0; pass < 2 && n < max; pass++) {
    const home_game *list = pass == 0 ? g->recent : g->shelf;
    int count = pass == 0 ? g->recent_count : g->shelf_count;
    for (int i = 0; i < count && n < max; i++) {
      int id = list[i].id;
      bool seen = id == resume_id;
      for (int k = 0; k < n && !seen; k++)
        seen = out[k]->id == id;
      if (!seen)
        out[n++] = &list[i];
    }
  }
  return n;
}

#endif
