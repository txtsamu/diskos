#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
"""Guard: the Player routing frames (0666 out device, 0642 gadget role, 0657 work mode, 06c1 BT init, 0818 BT-RX prep)
are written in outseq.c and nowhere else in the UI sources. Run: python3 -I tests/outseq_guard_test.py"""
from pathlib import Path
import re
import unittest

APP = Path(__file__).resolve().parents[1]
ROUTING = re.compile(r'(0666|0642|0657|06c1|0818)000C', re.IGNORECASE)


class OutseqGuard(unittest.TestCase):
    def test_no_routing_frame_literal_outside_outseq(self):
        offenders = []
        for path in sorted(APP.glob('*.c')) + sorted(APP.glob('*.h')):
            if path.name in ('outseq.c', 'sqlite3.c') or path.name.startswith('font_'):
                continue
            for n, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
                if ROUTING.search(line):
                    offenders.append(f'{path.name}:{n}: {line.strip()[:100]}')
        self.assertEqual(offenders, [], 'routing frames must go through outseq.c:\n' + '\n'.join(offenders))

    def test_play_preamble_only_sent_by_admit(self):
        call = re.compile(r'\boutseq_(local_preamble|assert_workmode)\s*\(')
        offenders = []
        for path in sorted(APP.glob('*.c')):
            if path.name in ('outseq.c', 'admit.c', 'sqlite3.c') or path.name.startswith('font_'):
                continue
            for n, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
                if call.search(line):
                    offenders.append(f'{path.name}:{n}: {line.strip()[:100]}')
        self.assertEqual(offenders, [], 'play-start frames must go through admit_play_start():\n' + '\n'.join(offenders))
        self.assertTrue(call.search((APP / 'admit.c').read_text()))

    def test_playpause_frame_spelled_once(self):
        offenders = []
        for path in sorted(APP.glob('*.c')) + sorted(APP.glob('*.h')):
            if path.name in ('ipc.h', 'theme_kit.c', 'sqlite3.c') or path.name.startswith('font_'):
                continue
            for n, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
                if '0201000C0000' in line:
                    offenders.append(f'{path.name}:{n}: {line.strip()[:100]}')
        self.assertEqual(offenders, [], 'use PLAYER_CMD_PLAYPAUSE:\n' + '\n'.join(offenders))
        self.assertIn('#define PLAYER_CMD_PLAYPAUSE "0201000C0000"', (APP / 'ipc.h').read_text())

    def test_admission_not_recomposed_by_callers(self):
        facts = re.compile(r'\b(sd_local_playback_allowed|modes_output_busy|ui_output_blocked|ui_player_settling)\b')
        owners = {'srcswitch.c', 'srcswitch.h', 'sdio.c', 'sdio.h', 'modes.c', 'modes.h', 'admit.c', 'admit.h'}
        offenders = []
        for path in sorted(APP.glob('*.c')) + sorted(APP.glob('*.h')):
            if path.name in owners or path.name in ('sqlite3.c', 'sqlite3.h') or path.name.startswith('font_'):
                continue
            for n, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
                if not facts.search(line):
                    continue
                if path.name == 'main.c' and ('.local_playback_allowed' in line or 'Output is switching' in line):
                    continue
                offenders.append(f'{path.name}:{n}: {line.strip()[:100]}')
        self.assertEqual(offenders, [], 'ask admit_check() instead of combining the facts:\n' + '\n'.join(offenders))

    def test_outseq_still_owns_them(self):
        self.assertTrue(ROUTING.search((APP / 'outseq.c').read_text()))


if __name__ == '__main__':
    unittest.main()
