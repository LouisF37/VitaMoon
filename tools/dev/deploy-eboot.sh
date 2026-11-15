#!/bin/bash
# Fast iteration: replaces eboot.bin of the installed app and starts it again
# (vitacompanion on the Vita: FTP on port 1337, commands on port 1338). Only
# for changes to the code: assets and LiveArea need a full VPK install.
#   VITA_IP=192.168.1.20 tools/dev/deploy-eboot.sh [build folder]
set -e
cd "$(dirname "$0")/../.."
source tools/dev/vita-ip.sh
build="${1:-build}"
cmake --build "$build" -j"$(nproc)"
echo destroy | nc "$VITA_IP" 1338 || true   # stop the app if it is running
sleep 1
curl -T "$build/eboot.bin" "ftp://$VITA_IP:1337/ux0:/app/$TITLE_ID/eboot.bin"
echo "launch $TITLE_ID" | nc "$VITA_IP" 1338
