/*
 * Home screen (src/gui/home_logic.h): where the cursor goes between the side
 * menu and the previews' panels, the file each PC's library leaves for the
 * home, the PC the dashboard shows and the games of the row under its large card.
 */
#include "test.h"
#include "home_logic.h"

#include <stdlib.h>
#include <string.h>

static home_nav menu_at(int m) {
  home_nav n = {false, m, 0};
  return n;
}

static home_nav item_at(int m, int item) {
  home_nav n = {true, m, item};
  return n;
}

/* Home as ui_home.c lays it out: the large card on row 0 over six columns,
 * then `games` covers and "See all" in column 5. */
static home_panel home_panel_of(int games) {
  home_panel p;
  memset(&p, 0, sizeof(p));
  home_panel_add(&p, 0, 0, 6);
  for (int k = 0; k < games; k++)
    home_panel_add(&p, 1, k, 1);
  home_panel_add(&p, 1, 5, 1);
  return p;
}

/* The Settings preview: four cards, then three, the last over two columns. */
static home_panel settings_panel(void) {
  home_panel p;
  memset(&p, 0, sizeof(p));
  for (int c = 0; c < 7; c++)
    home_panel_add(&p, c < 4 ? 0 : 1, c < 4 ? c : c - 4, c == 6 ? 2 : 1);
  return p;
}

int main(void) {
  TEST_BEGIN("home screen");
  home_panel none;
  memset(&none, 0, sizeof(none));
  home_panel home = home_panel_of(5);
  home_panel settings = settings_panel();

  /* ---- the side menu ---- */
  home_nav n = menu_at(HOME_MENU_HOME);
  CHECK(home_nav_key(&n, HOME_KEY_UP, &home) == HOME_ACT_NONE && n.menu == HOME_MENU_HOME,
        "up on the first entry stays there");
  for (int i = 0; i < 8; i++)
    home_nav_key(&n, HOME_KEY_DOWN, &none);
  CHECK(n.menu == HOME_MENU_QUIT && !n.in_panel, "down goes through the menu (Help included) and stops on Quit");
  CHECK(HOME_MENU_HELP == HOME_MENU_SETTINGS + 1 && HOME_MENU_QUIT == HOME_MENU_HELP + 1, "Help comes before Quit");

  for (int m = HOME_MENU_LIBRARY; m <= HOME_MENU_QUIT; m++) {
    n = menu_at(m);
    CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, &settings) == HOME_ACT_OPEN && !n.in_panel && n.menu == m,
          "X on an entry other than Home opens its page (a shortcut)");
  }
  n = menu_at(HOME_MENU_SETTINGS);
  CHECK(home_nav_key(&n, HOME_KEY_RIGHT, &settings) == HOME_ACT_NONE && n.in_panel && n.item == 0 &&
            n.menu == HOME_MENU_SETTINGS,
        "right on Settings enters its preview, on the first card");
  n = menu_at(HOME_MENU_QUIT);
  CHECK(home_nav_key(&n, HOME_KEY_RIGHT, &none) == HOME_ACT_NONE && !n.in_panel,
        "right on an entry with nothing to pick (Quit, an empty library) does nothing");
  n = menu_at(HOME_MENU_SETTINGS);
  CHECK(home_nav_key(&n, HOME_KEY_BACK, &settings) == HOME_ACT_NONE && n.menu == HOME_MENU_HOME && !n.in_panel,
        "O in the menu goes straight back to Home");
  n = menu_at(HOME_MENU_HOME);
  CHECK(home_nav_key(&n, HOME_KEY_RIGHT, &home) == HOME_ACT_NONE && n.in_panel && n.item == 0,
        "right on Home enters its panel, on the large card");
  n = menu_at(HOME_MENU_HOME);
  CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, &home) == HOME_ACT_NONE && n.in_panel && n.item == 0,
        "X on Home enters it too (Home has no page of its own)");
  n = menu_at(HOME_MENU_LIBRARY);
  home_nav_key(&n, HOME_KEY_LEFT, &home);
  CHECK(!n.in_panel && n.menu == HOME_MENU_LIBRARY, "left in the menu does nothing");

  /* ---- Home's panel ---- */
  n = item_at(HOME_MENU_HOME, 0);
  home_nav_key(&n, HOME_KEY_RIGHT, &home);
  CHECK(n.in_panel && n.item == 0, "right on the large card stays there");
  home_nav_key(&n, HOME_KEY_DOWN, &home);
  CHECK(n.item == 1, "down from the large card: the first cover of the row");
  home_nav_key(&n, HOME_KEY_RIGHT, &home);
  home_nav_key(&n, HOME_KEY_RIGHT, &home);
  CHECK(n.item == 3, "right along the row");
  for (int i = 0; i < 6; i++)
    home_nav_key(&n, HOME_KEY_RIGHT, &home);
  CHECK(n.item == 6, "the row ends on See all and stays there");
  home_nav_key(&n, HOME_KEY_UP, &home);
  CHECK(n.item == 0, "up from anywhere in the row: the large card");
  home_nav_key(&n, HOME_KEY_UP, &home);
  CHECK(n.item == 0 && n.in_panel, "up on the top row stays there");
  n = item_at(HOME_MENU_HOME, 3);
  CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, &home) == HOME_ACT_ITEM && n.item == 3, "X on an item acts on it");
  n = item_at(HOME_MENU_HOME, 1);
  CHECK(home_nav_key(&n, HOME_KEY_LEFT, &home) == HOME_ACT_NONE && !n.in_panel && n.menu == HOME_MENU_HOME,
        "left past the first column: back to the menu, on the same entry");
  n = item_at(HOME_MENU_HOME, 0);
  home_nav_key(&n, HOME_KEY_LEFT, &home);
  CHECK(!n.in_panel && n.menu == HOME_MENU_HOME, "left from the large card: the menu too");
  n = item_at(HOME_MENU_HOME, 4);
  home_nav_key(&n, HOME_KEY_BACK, &home);
  CHECK(!n.in_panel && n.menu == HOME_MENU_HOME, "O in a panel goes back to the menu");

  home_panel two = home_panel_of(2);
  n = item_at(HOME_MENU_HOME, 2);
  home_nav_key(&n, HOME_KEY_RIGHT, &two);
  CHECK(n.item == 3, "a row of two games: right from the second goes to See all, past the empty columns");
  n = item_at(HOME_MENU_HOME, 0);
  home_nav_key(&n, HOME_KEY_DOWN, &two);
  CHECK(n.item == 1, "and down from the large card, its first game");
  home_panel only = home_panel_of(0);
  n = item_at(HOME_MENU_HOME, 0);
  home_nav_key(&n, HOME_KEY_DOWN, &only);
  CHECK(n.item == 1, "no game in the row: down reaches See all");

  /* ---- the Settings cards: two rows, the last card over two columns ---- */
  n = item_at(HOME_MENU_SETTINGS, 0);
  home_nav_key(&n, HOME_KEY_RIGHT, &settings);
  CHECK(n.item == 1, "right: Video, next to General");
  home_nav_key(&n, HOME_KEY_DOWN, &settings);
  CHECK(n.item == 5, "down from Video: Advanced, under it");
  n = item_at(HOME_MENU_SETTINGS, 3);
  home_nav_key(&n, HOME_KEY_DOWN, &settings);
  CHECK(n.item == 6, "down from Controls: Tests, whose card reaches under it");
  home_nav_key(&n, HOME_KEY_UP, &settings);
  CHECK(n.item == 2, "up from Tests: the card over its first column (Performance)");
  home_nav_key(&n, HOME_KEY_DOWN, &settings);
  home_nav_key(&n, HOME_KEY_LEFT, &settings);
  CHECK(n.item == 5, "left from Tests: Advanced");
  n = item_at(HOME_MENU_SETTINGS, 4);
  CHECK(home_nav_key(&n, HOME_KEY_LEFT, &settings) == HOME_ACT_NONE && !n.in_panel && n.menu == HOME_MENU_SETTINGS,
        "left from Touch (first column): back to the menu, on Settings");

  /* ---- the panel changed under the cursor (data read again) ---- */
  n = item_at(HOME_MENU_HOME, 6);
  home_nav_fix(&n, &two);
  CHECK(n.in_panel && n.item == 0, "an item gone: the cursor goes to the first one");
  n = menu_at(HOME_MENU_COMPUTERS);
  home_nav_fix(&n, &two);
  CHECK(!n.in_panel && n.menu == HOME_MENU_COMPUTERS, "on the menu, the cursor stays where it is");
  n = item_at(HOME_MENU_LIBRARY, 3);
  home_nav_fix(&n, &none);
  CHECK(!n.in_panel && n.menu == HOME_MENU_LIBRARY, "nothing left to pick: back to the menu, on the same entry");
  CHECK(home_panel_move(&none, 0, HOME_KEY_DOWN) == HOME_MOVE_STAY, "an empty panel: no move");
  home_panel full;
  memset(&full, 0, sizeof(full));
  for (int i = 0; i < HOME_PANEL_MAX; i++)
    home_panel_add(&full, i / 6, i % 6, 1);
  CHECK(home_panel_add(&full, 9, 0, 1) == -1 && full.count == HOME_PANEL_MAX, "a full panel refuses more items");

  /* ---- the PC's file ---- */
  static home_games g, back;
  memset(&g, 0, sizeof(g));
  snprintf(g.covers, sizeof(g.covers), "ux0:data/vitamoon/boxart/AA-BB-CC-DD-EE-FF");
  snprintf(g.opened, sizeof(g.opened), "2026-11-17 18:02");
  g.total = 9;
  const struct { int id; const char *name, *when; bool fav; } games[] = {
    {1006, "Hollow Knight", "2026-11-10 18:05", true}, {1000, "Final Fantasy IX", "2026-11-17 17:30", false},
    {1002, "Chrono\tCross", "2026-11-16 21:10", false}, {1007, "Celeste", "", false},
    {1003, "Duck\nStation", "", false},
  };
  for (int i = 0; i < 5; i++) {
    home_game *s = &g.shelf[g.shelf_count++];
    s->id = games[i].id;
    s->favourite = games[i].fav;
    home_copy_field(s->when, sizeof(s->when), games[i].when);
    home_copy_field(s->name, sizeof(s->name), games[i].name);
    home_add_recent(&g, games[i].id, games[i].when, games[i].name);
  }
  CHECK(g.recent_count == 3 && g.recent[0].id == 1000 && g.recent[1].id == 1002 && g.recent[2].id == 1006,
        "recent games: the ones played, the latest first; never-played ones left out");
  CHECK(!strcmp(g.shelf[2].name, "Chrono Cross") && !strcmp(g.shelf[4].name, "Duck Station"),
        "a tab or a line break in a name becomes a space (one record per line)");

  char text[4096];
  int len = home_games_format(&g, text, sizeof(text));
  CHECK(len > 0 && (size_t)len == strlen(text), "the file's text is written");
  CHECK(home_games_format(&g, text, 40) < 0, "a buffer too small is refused, not overrun");
  len = home_games_format(&g, text, sizeof(text));
  char copy[4096];
  memcpy(copy, text, (size_t)len + 1);
  CHECK(home_games_parse(copy, &back), "and read back");
  CHECK(!strcmp(back.covers, g.covers) && !strcmp(back.opened, g.opened) && back.total == 9,
        "covers folder, when it was left and the count come back");
  bool same = back.shelf_count == g.shelf_count && back.recent_count == g.recent_count;
  for (int i = 0; same && i < g.shelf_count; i++)
    same = back.shelf[i].id == g.shelf[i].id && back.shelf[i].favourite == g.shelf[i].favourite &&
           !strcmp(back.shelf[i].when, g.shelf[i].when) && !strcmp(back.shelf[i].name, g.shelf[i].name);
  for (int i = 0; same && i < g.recent_count; i++)
    same = back.recent[i].id == g.recent[i].id && !strcmp(back.recent[i].when, g.recent[i].when) &&
           !strcmp(back.recent[i].name, g.recent[i].name);
  CHECK(same, "every game comes back as it was (never played: empty date)");

  char crlf[] = "vitamoon-home 1\r\ncovers\tux0:x\r\ntotal\t2\r\nshelf\t5\t0\t-\tA game\r\n";
  CHECK(home_games_parse(crlf, &back) && back.shelf_count == 1 && !strcmp(back.shelf[0].name, "A game") &&
            !strcmp(back.covers, "ux0:x") && back.total == 2,
        "Windows line ends are read too");
  char other[] = "something else\nshelf\t5\t0\t-\tA\n";
  CHECK(!home_games_parse(other, &back) && back.shelf_count == 0, "a file that is not one is refused");
  char empty[] = "";
  CHECK(!home_games_parse(empty, &back), "an empty file is refused");
  char cut[] = "vitamoon-home 1\nshelf\t5\t1\nrecent\t7\nshelf\t6\t0\t-\tOK\nunknown\tx\n";
  CHECK(home_games_parse(cut, &back) && back.shelf_count == 1 && back.shelf[0].id == 6 && back.recent_count == 0,
        "cut lines and unknown kinds are skipped");
  char many[8192];
  size_t at = (size_t)snprintf(many, sizeof(many), "vitamoon-home 1\n");
  for (int i = 0; i < 30; i++)
    at += (size_t)snprintf(many + at, sizeof(many) - at, "shelf\t%d\t0\t-\tGame %d\nrecent\t%d\t2026-11-17 10:00\tG\n",
                           i, i, i);
  CHECK(home_games_parse(many, &back) && back.shelf_count == HOME_SHELF_MAX && back.recent_count == HOME_RECENT_MAX,
        "more lines than the home shows: the first ones are kept");

  /* ---- which PC the dashboard shows ---- */
  const char *opened[] = {"2026-11-16 20:00", "", "2026-11-17 18:02", NULL, "2026-11-17 09:00"};
  CHECK(home_pick_host(opened, 5) == 2, "the PC whose library was left last");
  const char *no_file[] = {"", NULL};
  CHECK(home_pick_host(no_file, 2) == -1, "no PC with a file: none (the caller takes the first paired one)");
  const char *tie[] = {"2026-11-17 18:02", "2026-11-17 18:02"};
  CHECK(home_pick_host(tie, 2) == 0, "a tie: the first one");

  /* ---- the row under the large card ---- */
  home_row_game row[5];
  int k = home_row_games(&g, 1000, row, 5);
  CHECK(k == 4 && row[0].game->id == 1002 && row[0].kind == HOME_ROW_RECENT && row[1].game->id == 1006 &&
            row[1].kind == HOME_ROW_RECENT,
        "first the other recent games, latest first, not the one of the large card");
  CHECK(row[2].game->id == 1007 && row[2].kind == HOME_ROW_GAMES && row[3].game->id == 1003,
        "then the library's next ones (the favourite was already a recent game: not twice)");
  g.shelf[3].favourite = true;   /* Celeste */
  k = home_row_games(&g, 1000, row, 5);
  CHECK(k == 4 && row[2].game->id == 1007 && row[2].kind == HOME_ROW_FAVOURITE && row[3].kind == HOME_ROW_GAMES,
        "a favourite comes after the recent games, before the rest");
  static home_games lots;
  memset(&lots, 0, sizeof(lots));
  for (int i = 0; i < 5; i++) {
    char when[20];
    snprintf(when, sizeof(when), "2026-11-1%d 10:00", i);
    home_add_recent(&lots, 100 + i, when, "G");
  }
  for (int i = 0; i < 4; i++) {
    lots.shelf[lots.shelf_count].id = 200 + i;
    lots.shelf[lots.shelf_count++].favourite = true;
  }
  k = home_row_games(&lots, lots.recent[0].id, row, 4);
  CHECK(k == 4 && row[0].kind == HOME_ROW_RECENT && row[1].kind == HOME_ROW_RECENT &&
            row[2].kind == HOME_ROW_FAVOURITE && row[3].kind == HOME_ROW_FAVOURITE,
        "a row of four: two recent games, then the favourites");
  for (int i = 0; i < lots.shelf_count; i++)
    lots.shelf[i].favourite = false;
  k = home_row_games(&lots, lots.recent[0].id, row, 4);
  CHECK(lots.recent_count == HOME_RECENT_MAX && k == 4 && row[2].kind == HOME_ROW_RECENT &&
            row[2].game->id == lots.recent[3].id && row[3].kind == HOME_ROW_GAMES && row[3].game->id == 200,
        "no favourite: the other recent game comes next, then the library");
  home_games one;
  memset(&one, 0, sizeof(one));
  one.shelf_count = 1;
  one.shelf[0].id = 5;
  CHECK(home_row_games(&one, 5, row, 5) == 0, "a PC with a single game: an empty row");
  CHECK(home_row_games(&one, 0, row, 5) == 1 && row[0].game->id == 5, "no game for the large card: the library's first");

  return TEST_END();
}
