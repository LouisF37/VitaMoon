#!/bin/bash
# Builds the UI preview (a PC program that renders the menus to PNG files).
# Needs: gcc, cairo and freetype development files, and VITASDK for the headers.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${1:-$here/out}"
: "${VITASDK:?set VITASDK to the Vita SDK folder (headers only are used)}"
mkdir -p "$out/inc"
# Only the Vita-specific headers: the SDK's C library headers must not shadow the PC's.
for d in psp2 psp2common vitasdk; do ln -sfn "$VITASDK/arm-vita-eabi/include/$d" "$out/inc/$d"; done
ln -sfn "$VITASDK/arm-vita-eabi/include/vita2d.h" "$out/inc/vita2d.h"

srcs="$here/fake_vita2d.c $here/preview_sys.c $here/preview_main.c
  $root/src/gui/guilib.c $root/src/gui/theme.c $root/src/gui/ui_settings.c $root/src/gui/ui_library.c
  $root/src/gui/ui.c $root/src/gui/ui_connect.c
  $root/src/i18n.c $root/src/config.c $root/third_party/inih/ini.c"
gcc -std=gnu99 -O1 -g -w -fcommon -D__vita__ -DUI_PREVIEW -DVERSION_MAJOR=0 -DVERSION_MINOR=6 -DVERSION_PATCH=0 -DBUILD_ID=\"preview\" \
  -DASSET_PREFIX="\"$root/assets/\"" \
  -I"$here" -I"$out/inc" -I"$root/src" -I"$root/src/gui" -I"$root/libgamestream" -I"$root/third_party/inih" \
  -I"$root/third_party/moonlight-common-c/src" -I"$root/third_party/libuuid/src" \
  $(pkg-config --cflags cairo freetype2) \
  $srcs -o "$out/preview" \
  $(pkg-config --libs cairo freetype2) -lm
echo "built $out/preview"
