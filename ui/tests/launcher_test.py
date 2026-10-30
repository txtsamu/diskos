#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
"""Link the real launch gate (launcher.c, with handoff.c) and verify EVERY failure still reaches the stock player exec.

Owner constraint: diskOS may disable its own features, never the user's player or their route
back to the stock firmware. An earlier version held forever here; that is the bug this guards.

tests/launcher_driver.c supplies the fakes (the exec and the launcher_env table); the paths the module reads are its
overridable macros, pointed at a temp dir here."""
from pathlib import Path
import os
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
driver = app / 'tests' / 'launcher_driver.c'


def build(root, name, card):
    status = root / 'status'
    blog = root / 'boot.log'
    exe = root / name
    subprocess.run([os.environ.get('CC', 'cc'), '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
                    '-I', str(app),
                    '-DLAUNCH_STATUS_PATH="%s"' % status, '-DBOOT_LOG_PATH="%s"' % blog,
                    '-DPLAYER_SYSCONFIG_DB="%s"' % (root / 'sysconfig.db'), '-DPLAYER_SD_CARD_NODE="%s"' % card,
                    '-DLAUNCH_DEADLINE_S=2', '-DPLAYER_COMM_NAME="test"',
                    str(driver), str(app / 'launcher.c'), str(app / 'handoff.c'), '-o', str(exe)], check=True)
    return exe, status, blog


with tempfile.TemporaryDirectory(prefix='diskos-launcher-test-') as temp:
    root = Path(temp)
    card = root / 'card'
    exe, status, blog = build(root, 'test', card)
    subprocess.run([str(exe)], check=True)
    for case, expect in [('guard', 'guard=0'), ('host', 'local=0'), ('reset', 'local=0'), ('late', 'local=0')]:
        if status.exists(): status.unlink()
        out = subprocess.check_output([str(exe), case], timeout=10)
        assert out == b'PLAYER_EXEC_REACHED\n', (case, out)
        got = status.read_text()
        assert expect in got, (case, got)
    for case in ['ok', 'transient', 'fresh']:
        if status.exists(): status.unlink()
        assert subprocess.check_output([str(exe), case], timeout=10) == b'PLAYER_EXEC_REACHED\n'
        assert status.read_text().startswith('guard=1 local=1'), (case, status.read_text())
    assert 'UNPROTECTED' in blog.read_text()

    if status.exists(): status.unlink()
    assert subprocess.check_output([str(exe), 'hang'], timeout=20) == b'PLAYER_EXEC_REACHED\n'

    blog.unlink(); os.mkfifo(blog)
    if status.exists(): status.unlink()
    assert subprocess.check_output([str(exe), 'blockedlog'], timeout=20) == b'PLAYER_EXEC_REACHED\n'
    print('PASS a hung preparation step and a blocked log both still reach the stock player')
    print('PASS every failure still reaches the stock player exec, with its reason published')

with tempfile.TemporaryDirectory(prefix='diskos-first-run-test-') as temp:
    root = Path(temp)
    parent = root / 'not-a-directory'
    plain, _, _ = build(root, 'plain', root / 'card')
    behind_file, _, _ = build(root, 'behind', parent / 'card')
    db = root / 'sysconfig.db'
    card = root / 'card'

    def check(expected, exe=plain):
        assert subprocess.check_output([str(exe), 'firstrun']) == f'{expected}\n'.encode()
    check(1)
    card.touch(); check(0); card.unlink()
    db.touch(); check(0); db.unlink()
    parent.touch(); check(0, behind_file)
    print('PASS first-run initialization requires both absent config and confirmed absent card')

print('COMPLETE launcher_test.py')
