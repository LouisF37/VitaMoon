/*
 * Menu helpers kept free of Vita calls: whether the title font has every
 * character of a text (src/gui/text_cover.h with the table that
 * tools/art/make_fonts.py writes), which picture of the waiting moon to show
 * (src/gui/moon.h), and that the characters rendered at start-up
 * (src/gui/prewarm.h) hold every character of the translations (src/i18n.c).
 */
#include "test.h"
#include "text_cover.h"
#include "title_font_cover.h"
#include "moon.h"
#include "reconnect.h"
#include "prewarm.h"
#include "i18n.c"   /* its table, to read every translation */

#include <Limelight.h>

static bool title_has(const char *text) {
  return text_covered(text, k_title_cover, TITLE_COVER_COUNT);
}

int main(void) {
  TEST_BEGIN("title font coverage");
  CHECK(title_has(""), "an empty text needs nothing");
  CHECK(title_has("Final Fantasy IX"), "plain Latin");
  CHECK(title_has("Connexion \xC3\xA0 MINI-PC..."), "French accents (\xC3\xA0)");
  CHECK(title_has("R\xC3\xA9glages\nG\xC3\xA9n\xC3\xA9ral"), "a line break is not a character to draw");
  CHECK(title_has("Pok\xC3\xA9mon \xE2\x80\x94 \xC2\xAB Ed. \xC2\xBB"), "dash and French quotes");
  CHECK(!title_has("\xD0\x96\xD0\xB8\xD0\xB7\xD0\xBD\xD1\x8C"), "Cyrillic falls back to Inter");
  CHECK(!title_has("\xCE\xA9mega"), "Greek falls back to Inter");
  CHECK(!title_has("\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\x8A\xE3\x83\xAB"), "Japanese falls back to Inter");
  CHECK(!title_has("abc\xFF"), "a malformed byte falls back");
  CHECK(!title_has("abc\xC3"), "a sequence cut short falls back");

  const char *p = "\xE2\x82\xAC";
  CHECK(utf8_next(&p) == 0x20AC && *p == 0, "three-byte sequence decoded (euro sign)");
  p = "\xF0\x9F\x8E\xAE!";
  CHECK(utf8_next(&p) == 0x1F3AE && *p == '!', "four-byte sequence decoded, next character kept");

  TEST_BEGIN("waiting moon");
  const int stages = STAGE_VIDEO_STREAM_START - STAGE_PLATFORM_INIT;
  CHECK(moon_phase(0, stages) == 0, "first stage: new moon");
  CHECK(moon_phase(stages, stages) == MOON_PHASES - 1, "video start: full moon (the mark)");
  bool rising = true;
  for (int i = 1; i <= stages; i++)
    rising = rising && moon_phase(i, stages) > moon_phase(i - 1, stages);
  CHECK(rising, "every stage shows a fuller moon than the one before");
  CHECK(moon_phase(0, RECONNECT_GIVE_UP_MS) == 0, "reconnection starts with a new moon");
  CHECK(moon_phase(RECONNECT_GIVE_UP_MS / 2, RECONNECT_GIVE_UP_MS) == 6, "half the minute of tries: phase 6 of 11");
  CHECK(moon_phase(RECONNECT_GIVE_UP_MS + 5000, RECONNECT_GIVE_UP_MS) == MOON_PHASES - 1, "past the minute: full");
  CHECK(moon_phase(3, 0) == MOON_PHASES - 1, "no steps: full, no division by zero");

  CHECK(moon_should_draw(0, 1000, 3), "first picture always drawn");
  CHECK(!moon_should_draw(1000000, 1020000, 4), "not again 20 ms later");
  CHECK(moon_should_draw(1000000, 1050000, 4), "again 50 ms later");
  CHECK(moon_should_draw(1000000, 1001000, MOON_PHASES - 1), "the full moon is never skipped");

  /* A character missing from the start-up set is rendered the first time it
   * is drawn: in the middle of an animation, or by the stream's display thread
   * for the quick menu and the statistics. */
  TEST_BEGIN("characters rendered at start-up");
  int missing = 0, entries = (int)(sizeof(table) / sizeof(table[0]));
  for (int i = 0; i < entries; i++) {
    const char *texts[3] = {table[i].en, table[i].fr, table[i].es};
    for (int t = 0; t < 3; t++) {
      for (const char *p = texts[t]; p && *p;) {
        const char *start = p;
        unsigned int c = utf8_next(&p);
        if (c == '\n')
          continue;
        bool found = false;
        for (const char *q = k_prewarm; *q && !found;)
          found = utf8_next(&q) == c;
        if (!found && missing++ < 5)
          printf("    not rendered at start-up: U+%04X in \"%.40s\"\n", c, start);
      }
    }
  }
  CHECK(missing == 0, "every character of the translations is rendered at start-up");
  return TEST_END();
}
