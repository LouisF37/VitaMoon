#!/bin/bash
# Fetches the newest crash dump (ux0:data/psp2core-*.psp2dmp) from the Vita over
# FTP and decodes it against build/moonlight.elf with vita-parse-core
# (https://github.com/xyzz/vita-parse-core, path in $PARSE_CORE).
#   VITA_IP=192.168.1.20 PARSE_CORE=../vita-parse-core/main.py tools/dev/crash-dump.sh
set -e
cd "$(dirname "$0")/../.."
source tools/dev/vita-ip.sh
PARSE_CORE="${PARSE_CORE:-../vita-parse-core/vita-parse-core}"
latest=$(curl -s --user anonymous:anonymous "ftp://$VITA_IP:1337/ux0:/data/" | grep -o 'psp2core-[^ ]*\.psp2dmp' | sort | tail -n 1)
if [ -z "$latest" ]; then
  echo "No crash dump on the Vita."
  exit 1
fi
echo "Newest dump: $latest"
tmp=$(mktemp)
curl -s --user anonymous:anonymous "ftp://$VITA_IP:1337/ux0:/data/$latest" -o "$tmp"
"$PARSE_CORE" "$tmp" build/moonlight.elf
rm -f "$tmp"
