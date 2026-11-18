#!/bin/bash
# Renders every screen in the three languages and reports text that does not fit.
#   VITASDK=/path/to/vitasdk tools/ui-preview/run.sh [output folder]
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:-$here/out}"
"$here/build.sh" "$out" > /dev/null
: > "$out/report.txt"
for lang in en fr es; do
  mkdir -p "$out/$lang"
  for scenario in splash home home_empty home_no_games home_paused home_pc_off help_wifi home_wifi home_bt settings settings_change settings_pages sky_setting library library_stats library_running dialogs restart connecting reconnect quickmenu pairing; do
    "$out/preview" "$scenario" "$lang" "$out/$lang" 2>> "$out/report.$lang.tmp" || echo "$lang/$scenario: exit $?" >> "$out/report.txt"
  done
  sort -u "$out/report.$lang.tmp" | sed "s/^/[$lang] /" >> "$out/report.txt"
  rm -f "$out/report.$lang.tmp"
done
# Journeys: from opening the PC to the end of a game, through the real menus,
# src/connection.c and libgamestream, against an imitation of Sunshine
# (journeys.c, fake_pc.c, fake_http.c). Only failures are written.
mkdir -p "$out/journeys"
for journey in $("$out/preview" journeys en "$out/journeys"); do
  "$out/preview" "$journey" en "$out/journeys" 2> "$out/journeys/$journey.txt" || echo "journeys/$journey: exit $?" >> "$out/report.txt"
  grep -v "^TRUNCATED" "$out/journeys/$journey.txt" >> "$out/report.txt" || true
done

# The colour themes, on a few screens (French).
themes="night dawn aurora twilight eclipse"
i=0
for theme in $themes; do
  mkdir -p "$out/themes/$theme"
  for scenario in home library settings quickmenu sky; do
    PREVIEW_THEME=$i "$out/preview" "$scenario" fr "$out/themes/$theme" 2>/dev/null || echo "themes/$theme/$scenario: exit $?" >> "$out/report.txt"
  done
  i=$((i + 1))
done
echo "screens: $out/{en,fr,es}/*.png, $out/themes/*/*.png; journeys: $(ls "$out/journeys" | wc -l)"
if [ -s "$out/report.txt" ]; then echo "text that does not fit:"; cat "$out/report.txt"; else echo "all text fits"; fi
