#ifndef HOME_LOGIC_H
#define HOME_LOGIC_H

/* Home screen: where the cursor goes, and the small file that lets the home
 * show a PC's games without asking the PC. Kept free of Vita calls: tested on
 * a PC (tools/tests/test_home.c). The drawing is in ui_home.c.
 *
 * The home has a side menu (Home, Library, Computers, Settings, Help, Quit)
 * and, on its right, a preview of the entry under the cursor. Every preview
 * that holds something to pick is a panel the cursor can enter with the right
 * arrow: Home's large card and row of games, the library's covers, the PCs,
 * the settings' categories, the help's topics. Left from the first column, or
 * O, goes back to the menu; X acts on what is selected.
 *
 * The games come from <data folder>/<PC name>/home.txt, written by the library
 * when it is left (the library has just read the PC's list, the favourites and
 * the last sessions): the home reads one small file per PC and nothing else. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- the cursor ---------------- */

enum {
  HOME_MENU_HOME, HOME_MENU_LIBRARY, HOME_MENU_COMPUTERS, HOME_MENU_SETTINGS, HOME_MENU_HELP, HOME_MENU_QUIT,
  HOME_MENU_COUNT
};

/* A panel: what the cursor can stop on in a preview, laid out in rows and
 * columns. An item takes `span` columns from `col`; rows follow each other
 * from 0, without a gap. */
#define HOME_PANEL_MAX 16
typedef struct {
  signed char row, col, span;
} home_cell;

typedef struct {
  int count;
  home_cell cell[HOME_PANEL_MAX];
} home_panel;

/* Adds an item; returns its index (-1 when the panel is full). */
static inline int home_panel_add(home_panel *p, int row, int col, int span) {
  if (p->count >= HOME_PANEL_MAX)
    return -1;
  p->cell[p->count] = (home_cell){(signed char)row, (signed char)col, (signed char)(span < 1 ? 1 : span)};
  return p->count++;
}

typedef struct {
  bool in_panel;   /* in the preview of `menu` (the menu then shows `menu` as the current entry) */
  int menu;        /* HOME_MENU_* under the cursor, or whose preview the cursor is in */
  int item;        /* index in the panel, while in it */
} home_nav;

typedef enum { HOME_KEY_UP, HOME_KEY_DOWN, HOME_KEY_LEFT, HOME_KEY_RIGHT, HOME_KEY_CONFIRM, HOME_KEY_BACK } home_key;

typedef enum {
  HOME_ACT_NONE,
  HOME_ACT_OPEN,   /* open the page of nav->menu (Library, Computers, Settings, Help) or quit */
  HOME_ACT_ITEM    /* act on nav->item of nav->menu's panel */
} home_action;

#define HOME_MOVE_STAY (-1)
#define HOME_MOVE_OUT (-2)   /* left past the first column: back to the menu */

/* Where an arrow goes from `item`: another item, HOME_MOVE_STAY or HOME_MOVE_OUT.
 * Up and down go to the nearest row that has items, onto the item under the
 * column the cursor starts from (or the nearest one); left and right go along
 * the row. */
static inline int home_panel_move(const home_panel *p, int item, home_key key) {
  if (item < 0 || item >= p->count)
    return HOME_MOVE_STAY;
  home_cell c = p->cell[item];
  int best = -1;
  if (key == HOME_KEY_LEFT || key == HOME_KEY_RIGHT) {
    for (int i = 0; i < p->count; i++) {
      home_cell o = p->cell[i];
      if (o.row != c.row || i == item)
        continue;
      if (key == HOME_KEY_LEFT && o.col < c.col && (best < 0 || o.col > p->cell[best].col))
        best = i;
      if (key == HOME_KEY_RIGHT && o.col > c.col && (best < 0 || o.col < p->cell[best].col))
        best = i;
    }
    if (best >= 0)
      return best;
    return key == HOME_KEY_LEFT ? HOME_MOVE_OUT : HOME_MOVE_STAY;
  }
  if (key != HOME_KEY_UP && key != HOME_KEY_DOWN)
    return HOME_MOVE_STAY;
  int step = key == HOME_KEY_DOWN ? 1 : -1;
  int max_row = 0;
  for (int i = 0; i < p->count; i++)
    if (p->cell[i].row > max_row)
      max_row = p->cell[i].row;
  for (int row = c.row + step; row >= 0 && row <= max_row; row += step) {
    int best_dist = 0;
    for (int i = 0; i < p->count; i++) {
      home_cell o = p->cell[i];
      if (o.row != row)
        continue;
      int dist = c.col < o.col ? o.col - c.col : c.col >= o.col + o.span ? c.col - (o.col + o.span - 1) : 0;
      if (best < 0 || dist < best_dist) {
        best = i;
        best_dist = dist;
      }
    }
    if (best >= 0)
      return best;
  }
  return HOME_MOVE_STAY;
}

/* After the panel changed (data reloaded): keeps the cursor on an item. */
static inline void home_nav_fix(home_nav *nav, const home_panel *panel) {
  if (nav->menu < 0 || nav->menu >= HOME_MENU_COUNT)
    nav->menu = HOME_MENU_HOME;
  if (!nav->in_panel)
    return;
  if (panel->count == 0)
    nav->in_panel = false;
  else if (nav->item < 0 || nav->item >= panel->count)
    nav->item = 0;
}

/* Moves the cursor for one key; returns what to do. `panel`: the panel of
 * nav->menu's preview (empty when it has nothing to pick). */
static inline home_action home_nav_key(home_nav *nav, home_key key, const home_panel *panel) {
  if (!nav->in_panel) {
    switch (key) {
      case HOME_KEY_UP:
        if (nav->menu > 0) nav->menu--;
        return HOME_ACT_NONE;
      case HOME_KEY_DOWN:
        if (nav->menu < HOME_MENU_COUNT - 1) nav->menu++;
        return HOME_ACT_NONE;
      case HOME_KEY_RIGHT:
      case HOME_KEY_CONFIRM:
        /* Home has no page of its own: X enters its panel, as the right arrow */
        if (key == HOME_KEY_CONFIRM && nav->menu != HOME_MENU_HOME)
          return HOME_ACT_OPEN;
        if (panel->count > 0) {
          nav->in_panel = true;
          nav->item = 0;
        }
        return HOME_ACT_NONE;
      case HOME_KEY_BACK:
        nav->menu = HOME_MENU_HOME;   /* the shortcut back to Home */
        return HOME_ACT_NONE;
      default:
        return HOME_ACT_NONE;
    }
  }
  switch (key) {
    case HOME_KEY_CONFIRM:
      return HOME_ACT_ITEM;
    case HOME_KEY_BACK:
      nav->in_panel = false;
      return HOME_ACT_NONE;
    default: {
      int to = home_panel_move(panel, nav->item, key);
      if (to == HOME_MOVE_OUT)
        nav->in_panel = false;
      else if (to >= 0)
        nav->item = to;
      return HOME_ACT_NONE;
    }
  }
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

/* The row of games under the large card: up to two other games played last
 * (so that favourites show in a row of four), then the favourites, then the
 * other recent games and the start of the library, never one twice nor the
 * game of the large card (skip_id). Returns how many (<= max). */
enum { HOME_ROW_RECENT, HOME_ROW_FAVOURITE, HOME_ROW_GAMES };
#define HOME_ROW_RECENT_MAX 2

typedef struct {
  const home_game *game;
  int kind;   /* HOME_ROW_* */
} home_row_game;

static inline int home_row_games(const home_games *g, int skip_id, home_row_game out[], int max) {
  int n = 0;
  /* passes: the first recent games, the favourites, the other recent games, the library */
  static const int k_kind[4] = {HOME_ROW_RECENT, HOME_ROW_FAVOURITE, HOME_ROW_RECENT, HOME_ROW_GAMES};
  for (int pass = 0; pass < 4 && n < max; pass++) {
    bool recent = pass == 0 || pass == 2;
    const home_game *list = recent ? g->recent : g->shelf;
    int count = recent ? g->recent_count : g->shelf_count;
    int taken = 0;
    for (int i = 0; i < count && n < max; i++) {
      if (pass == 0 && taken >= HOME_ROW_RECENT_MAX)
        break;
      if (pass == 1 && !list[i].favourite)
        continue;
      int id = list[i].id;
      bool seen = id == skip_id;
      for (int k = 0; k < n && !seen; k++)
        seen = out[k].game->id == id;
      if (seen)
        continue;
      out[n++] = (home_row_game){&list[i], k_kind[pass]};
      taken++;
    }
  }
  return n;
}

#endif
