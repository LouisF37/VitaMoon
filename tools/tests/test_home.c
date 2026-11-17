/*
 * Home screen (src/gui/home_logic.h): where the cursor goes between the side
 * menu and the tiles, the file each PC's library leaves for the home, the PC
 * the dashboard shows and the games of its strip.
 */
#include "test.h"
#include "home_logic.h"

#include <stdlib.h>
#include <string.h>

#define ALL_TILES ((1u << HOME_TILE_COUNT) - 1)

static home_nav menu_at(int m) {
  home_nav n = {false, m, HOME_TILE_RESUME};
  return n;
}

static home_nav tile_at(int t) {
  home_nav n = {true, HOME_MENU_HOME, t};
  return n;
}

int main(void) {
  TEST_BEGIN("home screen");

  /* ---- the side menu ---- */
  home_nav n = menu_at(HOME_MENU_HOME);
  CHECK(home_nav_key(&n, HOME_KEY_UP, ALL_TILES) == HOME_ACT_NONE && n.menu == HOME_MENU_HOME,
        "up on the first entry stays there");
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  CHECK(n.menu == HOME_MENU_QUIT && !n.in_tiles, "down goes through the menu and stops on Quit");

  n = menu_at(HOME_MENU_LIBRARY);
  CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, ALL_TILES) == HOME_ACT_LIBRARY, "X on Library opens it");
  n = menu_at(HOME_MENU_COMPUTERS);
  CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, ALL_TILES) == HOME_ACT_COMPUTERS, "X on Computers opens it");
  n = menu_at(HOME_MENU_SETTINGS);
  CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, ALL_TILES) == HOME_ACT_SETTINGS, "X on Settings opens them");
  n = menu_at(HOME_MENU_QUIT);
  CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, ALL_TILES) == HOME_ACT_QUIT, "X on Quit quits");
  n = menu_at(HOME_MENU_SETTINGS);
  CHECK(home_nav_key(&n, HOME_KEY_RIGHT, ALL_TILES) == HOME_ACT_NONE && !n.in_tiles && n.menu == HOME_MENU_SETTINGS,
        "right on another entry than Home does nothing (X opens it)");

  n = menu_at(HOME_MENU_SETTINGS);
  CHECK(home_nav_key(&n, HOME_KEY_BACK, ALL_TILES) == HOME_ACT_NONE && n.menu == HOME_MENU_HOME && !n.in_tiles,
        "O in the menu goes straight back to Home (the shortcut)");

  n = menu_at(HOME_MENU_HOME);
  CHECK(home_nav_key(&n, HOME_KEY_RIGHT, ALL_TILES) == HOME_ACT_NONE && n.in_tiles && n.tile == HOME_TILE_RESUME,
        "right on Home enters the tiles, on the game to pick up");
  n = menu_at(HOME_MENU_HOME);
  CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, ALL_TILES) == HOME_ACT_NONE && n.in_tiles && n.tile == HOME_TILE_RESUME,
        "X on Home enters the tiles too");

  /* ---- the tiles ---- */
  n = tile_at(HOME_TILE_RESUME);
  home_nav_key(&n, HOME_KEY_RIGHT, ALL_TILES);
  CHECK(n.tile == HOME_TILE_PC, "right from the game to pick up: the PC");
  home_nav_key(&n, HOME_KEY_RIGHT, ALL_TILES);
  CHECK(n.tile == HOME_TILE_PC && n.in_tiles, "right on the PC (right edge) stays there");
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  CHECK(n.tile == HOME_TILE_ALL, "down from the PC: See all, under it");
  home_nav_key(&n, HOME_KEY_LEFT, ALL_TILES);
  CHECK(n.tile == HOME_TILE_GAME2, "left along the strip");
  home_nav_key(&n, HOME_KEY_UP, ALL_TILES);
  CHECK(n.tile == HOME_TILE_PC, "up from the right of the strip: the PC");

  n = tile_at(HOME_TILE_RESUME);
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  CHECK(n.tile == HOME_TILE_GAME0, "down from the game to pick up: the first game of the strip");
  home_nav_key(&n, HOME_KEY_RIGHT, ALL_TILES);
  home_nav_key(&n, HOME_KEY_UP, ALL_TILES);
  CHECK(n.tile == HOME_TILE_RESUME, "up from the left of the strip: the game to pick up");
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  home_nav_key(&n, HOME_KEY_DOWN, ALL_TILES);
  CHECK(n.tile == HOME_TILE_GAME0, "down on the bottom row stays there");

  n = tile_at(HOME_TILE_GAME0);
  CHECK(home_nav_key(&n, HOME_KEY_LEFT, ALL_TILES) == HOME_ACT_NONE && !n.in_tiles && n.menu == HOME_MENU_HOME,
        "left past the strip's first game: back to the menu, on Home");
  n = tile_at(HOME_TILE_RESUME);
  home_nav_key(&n, HOME_KEY_LEFT, ALL_TILES);
  CHECK(!n.in_tiles && n.menu == HOME_MENU_HOME, "left past the game to pick up: the menu too");
  n = tile_at(HOME_TILE_PC);
  home_nav_key(&n, HOME_KEY_BACK, ALL_TILES);
  CHECK(!n.in_tiles && n.menu == HOME_MENU_HOME, "O in the tiles goes back to the menu");
  n = tile_at(HOME_TILE_GAME1);
  CHECK(home_nav_key(&n, HOME_KEY_CONFIRM, ALL_TILES) == HOME_ACT_TILE && n.tile == HOME_TILE_GAME1,
        "X on a tile acts on it");

  /* tiles that are not there are skipped */
  unsigned few = HOME_TILE_BIT(HOME_TILE_RESUME) | HOME_TILE_BIT(HOME_TILE_PC) | HOME_TILE_BIT(HOME_TILE_GAME0) |
                 HOME_TILE_BIT(HOME_TILE_ALL);
  n = tile_at(HOME_TILE_GAME0);
  home_nav_key(&n, HOME_KEY_RIGHT, few);
  CHECK(n.tile == HOME_TILE_ALL, "a strip with one game: right goes from it to See all");
  home_nav_key(&n, HOME_KEY_UP, few);
  CHECK(n.tile == HOME_TILE_PC, "and up from See all to the PC");
  unsigned no_pc = HOME_TILE_BIT(HOME_TILE_RESUME) | HOME_TILE_BIT(HOME_TILE_ALL);
  n = tile_at(HOME_TILE_ALL);
  home_nav_key(&n, HOME_KEY_UP, no_pc);
  CHECK(n.tile == HOME_TILE_RESUME, "without the PC tile, up from See all reaches the game to pick up");
  n = tile_at(HOME_TILE_RESUME);
  home_nav_key(&n, HOME_KEY_DOWN, no_pc);
  CHECK(n.tile == HOME_TILE_ALL, "and down from it, See all");
  unsigned only = HOME_TILE_BIT(HOME_TILE_RESUME);
  n = tile_at(HOME_TILE_RESUME);
  home_nav_key(&n, HOME_KEY_DOWN, only);
  home_nav_key(&n, HOME_KEY_RIGHT, only);
  CHECK(n.tile == HOME_TILE_RESUME && n.in_tiles, "a single tile (no PC yet): the cursor stays on it");

  /* the tiles changed under the cursor (data read again) */
  n = tile_at(HOME_TILE_GAME2);
  home_nav_fix(&n, few);
  CHECK(n.in_tiles && n.tile == HOME_TILE_RESUME, "a game gone from the strip: the cursor goes to the first tile");
  n = menu_at(HOME_MENU_COMPUTERS);
  home_nav_fix(&n, few);
  CHECK(!n.in_tiles && n.menu == HOME_MENU_COMPUTERS, "on the menu, the cursor stays where it is");
  n = tile_at(HOME_TILE_PC);
  home_nav_fix(&n, 0);
  CHECK(!n.in_tiles, "no tile at all: back to the menu");

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
  const char *none[] = {"", NULL};
  CHECK(home_pick_host(none, 2) == -1, "no PC with a file: none (the caller takes the first paired one)");
  const char *tie[] = {"2026-11-17 18:02", "2026-11-17 18:02"};
  CHECK(home_pick_host(tie, 2) == 0, "a tie: the first one");

  /* ---- the strip under the game to pick up ---- */
  const home_game *strip[3];
  int k = home_strip_games(&g, 1000, strip, 3);
  CHECK(k == 3 && strip[0]->id == 1002 && strip[1]->id == 1006 && strip[2]->id == 1007,
        "the other recent games, then the library's next ones, none twice, not the one to pick up");
  home_games one;
  memset(&one, 0, sizeof(one));
  one.shelf_count = 1;
  one.shelf[0].id = 5;
  CHECK(home_strip_games(&one, 5, strip, 3) == 0, "a PC with a single game: an empty strip");
  CHECK(home_strip_games(&one, 0, strip, 3) == 1 && strip[0]->id == 5, "no game to pick up: the library's first");

  return TEST_END();
}
