#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../src/launcher/diskos_launch.c"
OUT="$HERE/../payload/diskos-launch"
CROSS=${CROSS:-mipsel-linux-musl-}
"${CROSS}gcc" -Os -static -Wall -Wextra -Werror -ffile-prefix-map="$HERE/../src/launcher/"= -o "$OUT" "$SRC"
"${CROSS}strip" "$OUT" || true
chmod 755 "$OUT"
sha256sum "$OUT"
