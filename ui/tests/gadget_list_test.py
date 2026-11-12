#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
"""The stock gadget names in ui/main.c and payload/diskos-debug.sh must match.

Both processes inspect the same configfs directory, but one is C and one is a shell script, so the list cannot be
shared at runtime. This test is the link: a rename on either side fails here instead of leaving the UI and usb-on with
different opinions about whether a stock gadget owns the port."""
from pathlib import Path
import re

app = Path(__file__).resolve().parents[1]
root = app.parent
c = (app / 'main.c').read_text()
sh = (root / 'payload' / 'diskos-debug.sh').read_text()

m = re.search(r'static const char \*const names\[\]\s*=\s*\{([^}]*)\}', c)
assert m, 'ui/main.c: stock gadget array not found'
c_names = re.findall(r'"([^"]+)"', m.group(1))

m = re.search(r'for _g in ([^;]+); do', sh)
assert m, 'payload/diskos-debug.sh: stock gadget loop not found'
sh_names = m.group(1).split()

assert c_names == sh_names, f'gadget lists differ: C {c_names} vs shell {sh_names}'
print('PASS stock gadget names match:', ' '.join(c_names))
print('COMPLETE gadget_list_test.py')
