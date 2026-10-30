#!/usr/bin/env bash
set -eu

IP="${DISKOS_IP:?set DISKOS_IP to the device IP (shown in Debug Mode)}"
export SSHPASS="${DISKOS_PW:?set DISKOS_PW to the Debug Mode SSH password}"
OUT="${1:-diskos-shot.png}"
RAW="$(mktemp)"
trap 'rm -f "$RAW"' EXIT

sshpass -e ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 "root@$IP" \
  'dd if=/dev/fb0 bs=518400 count=1 2>/dev/null' > "$RAW"

BYTES="$(wc -c < "$RAW" | tr -d ' ')"
[ "$BYTES" -ge 518400 ] || { echo "capture failed (short read: $BYTES bytes)" >&2; exit 1; }

python3 - "$RAW" "$OUT" <<'PY'
import sys
from PIL import Image
raw = open(sys.argv[1], "rb").read()[:518400]
img = Image.frombuffer("RGBA", (360, 360), raw, "raw", "BGRA").convert("RGB").rotate(180)
img.save(sys.argv[2])
print(sys.argv[2])
PY
