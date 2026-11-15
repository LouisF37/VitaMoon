#!/bin/bash
# PC-side tests of the parts of the app that do not need a Vita: pacing of
# the "Paced" display, settings file, SPS rewrite, URL encoding, session
# percentiles.
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

cc="gcc -std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
    -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -Werror=int-conversion"

$cc -I"$here" -I"$root/src/video" "$here/test_pacing.c" "$root/src/video/pacing.c" -o "$out/test_pacing"
$cc -I"$here" -I"$root/libgamestream" -I"$root/src/video" "$here/test_misc.c" "$root/libgamestream/url.c" -o "$out/test_misc"
$cc -w -fcommon -D__vita__ -I"$here" -I"$out/inc" -I"$root/src" -I"$root/third_party/inih" \
    -I"$root/third_party/moonlight-common-c/src" "$here/test_config.c" "$root/src/config.c" \
    "$root/third_party/inih/ini.c" -o "$out/test_config"

hb="$root/third_party/h264bitstream"
$cc -w -I"$here" -I"$root/libgamestream" -I"$hb" -I"$root/third_party/moonlight-common-c/src" \
    "$here/test_sps.c" "$root/libgamestream/sps.c" "$hb/h264_stream.c" "$hb/h264_nal.c" "$hb/h264_sei.c" \
    -o "$out/test_sps"

status=0
for t in test_pacing test_misc test_config test_sps; do
  "$out/$t" || status=1
done
if [ $status -eq 0 ]; then echo "all tests passed"; else echo "SOME TESTS FAILED"; fi
exit $status
