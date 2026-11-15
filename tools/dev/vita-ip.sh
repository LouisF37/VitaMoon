# Sourced by the scripts in tools/dev: the Vita's address, from $VITA_IP or
# from tools/dev/vita_ip.txt (one line, not committed: see .gitignore).
if [ -z "$VITA_IP" ]; then
  ip_file="$(dirname "${BASH_SOURCE[0]}")/vita_ip.txt"
  if [ -f "$ip_file" ]; then
    VITA_IP=$(head -n 1 "$ip_file" | tr -d '\r\n')
  fi
fi
if [ -z "$VITA_IP" ]; then
  echo "Set VITA_IP, or write the Vita's address in tools/dev/vita_ip.txt" >&2
  exit 1
fi
TITLE_ID=$(grep -Po 'set\(TITLE_ID\s+"\K[^"]+' "$(dirname "${BASH_SOURCE[0]}")/../../CMakeLists.txt" | head -n 1)
