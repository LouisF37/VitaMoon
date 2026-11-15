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
  for scenario in home home_empty home_wifi settings settings_change settings_pages library library_running dialogs pairing; do
    "$out/preview" "$scenario" "$lang" "$out/$lang" 2>> "$out/report.$lang.tmp" || echo "$lang/$scenario: exit $?" >> "$out/report.txt"
  done
  sort -u "$out/report.$lang.tmp" | sed "s/^/[$lang] /" >> "$out/report.txt"
  rm -f "$out/report.$lang.tmp"
done
echo "screens: $out/{en,fr,es}/*.png"
if [ -s "$out/report.txt" ]; then echo "text that does not fit:"; cat "$out/report.txt"; else echo "all text fits"; fi
