#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

[ "$(uname)" = "Darwin" ] || { echo "run this on macOS." >&2; exit 2; }

if [ "${1:-}" != "--skip-setup" ]; then
  echo ">> populating vendor/ for this Mac"
  bash vendor/setup-macos.sh
fi

VENV=build/venv
[ -d "$VENV" ] || python3 -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade pip pyinstaller -r requirements.txt

"$VENV/bin/pyinstaller" --clean --noconfirm \
  --distpath build/dist --workpath build/work \
  build/diskos-installer.spec

APP=build/dist/diskos-installer
codesign --force --deep --sign - "$APP" 2>/dev/null || \
  echo "   (codesign ad-hoc skipped/failed - 'xattr -dr com.apple.quarantine $APP' if Gatekeeper blocks it)"

echo
echo "Built: $APP ($(uname -m))"
echo "Smoke test: $APP doctor"
echo "If Gatekeeper blocks it: xattr -dr com.apple.quarantine $APP"
