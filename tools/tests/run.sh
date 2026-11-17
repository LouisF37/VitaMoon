#!/bin/bash
# PC-side tests of the parts of the app that do not need a Vita: pacing of
# the "Paced" display, late frames, press-to-picture test, settings file, SPS rewrite, URL encoding, session
# percentiles, quick menu, log ring, title font coverage, waiting moon,
# start-up screen, library order, home screen, twinkling stars and trackpad tap.
# Built with AddressSanitizer and UBSan.
#   VITASDK=/path/to/vitasdk tools/tests/run.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${1:-$here/out}"
: "${VITASDK:?set VITASDK to the Vita SDK folder (headers only are used)}"
mkdir -p "$out/inc"
# Only the Vita-specific headers: the SDK's C library headers must not shadow the PC's.
for d in psp2 psp2common vitasdk; do ln -sfn "$VITASDK/arm-vita-eabi/include/$d" "$out/inc/$d"; done
ln -sfn "$VITASDK/arm-vita-eabi/include/vita2d.h" "$out/inc/vita2d.h"

cc="gcc -std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
    -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -Werror=int-conversion"

$cc -I"$here" -I"$root/src/video" "$here/test_pacing.c" "$root/src/video/pacing.c" -o "$out/test_pacing"
$cc -I"$here" -I"$root/src/video" "$here/test_late.c" -o "$out/test_late"
$cc -I"$here" -I"$root/src/video" "$here/test_latency.c" -o "$out/test_latency"
$cc -I"$here" -I"$root/libgamestream" -I"$root/src/video" -I"$root/src" -I"$root/third_party/moonlight-common-c/src" \
    "$here/test_misc.c" "$root/libgamestream/url.c" -o "$out/test_misc"
$cc -w -fcommon -D__vita__ -I"$here" -I"$out/inc" -I"$root/src" -I"$root/third_party/inih" \
    -I"$root/third_party/moonlight-common-c/src" "$here/test_config.c" "$root/src/config.c" \
    "$root/third_party/inih/ini.c" -o "$out/test_config"

$cc -I"$here" -I"$out/inc" -I"$root/src/gui" "$here/test_quickmenu.c" -o "$out/test_quickmenu"
$cc -I"$here" -I"$root/src" "$here/test_log.c" "$root/src/log_core.c" -o "$out/test_log"
$cc -D__vita__ -I"$here" -I"$out/inc" -I"$root/third_party/mdnsniff" "$here/test_mdns.c" \
    "$root/third_party/mdnsniff/udp_sniffer_vita.c" -o "$out/test_mdns"
$cc -I"$here" -I"$root/src/gui" -I"$root/src" -I"$root/third_party/moonlight-common-c/src" \
    "$here/test_ui.c" -o "$out/test_ui"
$cc -I"$here" -I"$out/inc" -I"$root/src/gui" "$here/test_splash.c" -o "$out/test_splash" -lm
$cc -I"$here" -I"$root/src/gui" -I"$root/src/input" "$here/test_library.c" -o "$out/test_library" -lm
$cc -I"$here" -I"$root/src/gui" "$here/test_home.c" -o "$out/test_home"

hb="$root/third_party/h264bitstream"
$cc -w -I"$here" -I"$root/libgamestream" -I"$hb" -I"$root/third_party/moonlight-common-c/src" \
    "$here/test_sps.c" "$root/libgamestream/sps.c" "$hb/h264_stream.c" "$hb/h264_nal.c" "$hb/h264_sei.c" \
    -o "$out/test_sps"

status=0
for t in test_pacing test_late test_latency test_misc test_config test_sps test_quickmenu test_log test_ui test_mdns test_splash test_library test_home; do
  "$out/$t" || status=1
done
if [ $status -eq 0 ]; then echo "all tests passed"; else echo "SOME TESTS FAILED"; fi
exit $status
