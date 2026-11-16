#!/bin/bash
# Builds the VPK and sends it to VitaShell's FTP server (ux0:/ABM/), then
# starts VitaShell so it can be installed. Needs VitaShell's FTP (port 1337)
# and vitacompanion (port 1338) on the Vita.
#   VITA_IP=192.168.1.20 tools/dev/deploy-vpk.sh
set -e
cd "$(dirname "$0")/../.."
source tools/dev/vita-ip.sh
cmake -S . -B build
cmake --build build -j"$(nproc)"
curl --ftp-method nocwd -T build/vitamoon.vpk "ftp://$VITA_IP:1337/ux0:/ABM/"
echo "launch VITASHELL" | nc "$VITA_IP" 1338 || true
echo "Install ux0:/ABM/vitamoon.vpk in VitaShell, then: echo launch $TITLE_ID | nc $VITA_IP 1338"
