/*
 * Where the menus tell the PC preview (tools/ui-preview) what they show, so its
 * journeys can follow and answer them. Nothing in the Vita build.
 */
#pragma once

#ifdef UI_PREVIEW
void preview_on_screen(const char *name);
void preview_on_dialog(const char *kind, const char *text);
#define PREVIEW_SCREEN(name) preview_on_screen(name)
#define PREVIEW_DIALOG(kind, text) preview_on_dialog(kind, text)
#else
#define PREVIEW_SCREEN(name) ((void)0)
#define PREVIEW_DIALOG(kind, text) ((void)0)
#endif
