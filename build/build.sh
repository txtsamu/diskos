#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

VENV=build/venv
[ -d "$VENV" ] || python3 -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade pip pyinstaller -r requirements.txt

tag=$("$VENV/bin/python" - <<'PY'
import platform
s=platform.system().lower(); o={"darwin":"macos","linux":"linux"}.get(s,s)
m=platform.machine().lower()
a={"x86_64":"x86_64","amd64":"x86_64","arm64":"arm64","aarch64":"arm64"}.get(m,m)
print(f"{o}-{a}")
PY
)
[ -d "vendor/$tag" ] || { echo "ERROR: vendor/$tag missing - add native tools for this host first." >&2; exit 2; }

"$VENV/bin/pyinstaller" --clean --noconfirm \
  --distpath build/dist --workpath build/work \
  build/diskos-installer.spec

echo
echo "Built: build/dist/diskos-installer  ($(du -h build/dist/diskos-installer | cut -f1))"
echo "Smoke test: build/dist/diskos-installer doctor"
