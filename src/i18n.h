/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Interface translations. The interface is written in English; tr() returns
 * the text in the language chosen in the settings (English, French, Spanish),
 * or the English text itself when no translation exists.
 */
#pragma once

#include <stdbool.h>

enum { LANG_EN = 0, LANG_FR = 1, LANG_ES = 2, LANG_COUNT };

void i18n_set_language(int lang);
int i18n_language(void);
const char *i18n_language_name(int lang);

/* Marks an English string stored in a table and translated later with tr(). */
#define N_(text) text

/* Translation of an English interface string (may be a printf format). */
char *tr(const char *english);

/* Whether a count takes the plural in the current language (French keeps
 * the singular for 0, English and Spanish do not). */
bool i18n_plural(unsigned int count);
