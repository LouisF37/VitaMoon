/*
 * Characters drawn once at start-up, in every face and size, so that no glyph
 * has to be rendered in the middle of an animation or by the stream's display
 * thread: ASCII and what the French and Spanish translations use.
 * tools/tests/test_ui.c checks that every character of src/i18n.c is here.
 */
#pragma once

static const char k_prewarm[] =
  " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
  "abcdefghijklmnopqrstuvwxyz{|}~"
  "\xC3\xA0\xC3\xA2\xC3\xA4\xC3\xA7\xC3\xA9\xC3\xA8\xC3\xAA\xC3\xAB\xC3\xAE\xC3\xAF"
  "\xC3\xB4\xC3\xB6\xC3\xB9\xC3\xBB\xC3\xBC\xC3\xA1\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xB1"
  "\xC3\x80\xC3\x89\xC3\x88\xC3\x8A\xC3\x87\xC3\x81\xC3\x8D\xC3\x93\xC3\x9A\xC3\x91"
  "\xC2\xBF\xC2\xA1\xC2\xAB\xC2\xBB\xC2\xB0\xC2\xB7\xC3\x97\xE2\x80\xA6\xE2\x80\x99"
  /* no-break space (French punctuation), oe ligatures */
  "\xC2\xA0\xC5\x93\xC5\x92";
