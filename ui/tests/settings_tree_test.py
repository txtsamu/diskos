#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
"""Audit the Settings page tree in settings.c against tests/settings_baseline.tsv.

Every setting that was reachable before the regrouping must still exist exactly once, keep its type and its config key,
and be reachable from the five top groups through page rows. No page may be a dead end, orphaned, shared or too long.
Deep links (settings_open_key) must resolve to a real config key."""
from pathlib import Path
import collections
import re
import sys

app = Path(__file__).resolve().parents[1]
src = (app / 'settings.c').read_text()
MAX_ROWS = 9
TOP = ['Playback', 'Audio', 'Display', 'Network', 'System']
fails = []


def fail(msg):
    fails.append(msg)
    print('FAIL ' + msg)


block = src[src.index('static const setting_t TABLE[] = {'):src.index('#define N_SETTINGS')]
row_re = re.compile(r'\s*\{ "([^"]*)",\s*"([^"]*)",\s*(ST_\w+),\s*([^,]+),')
page_re = re.compile(r'"([^"]*)"\s*\},?\s*$')
rows = []
flag = ''
lines = block.splitlines()
for n, line in enumerate(lines):
    m = re.match(r'#ifdef (\w+)', line)
    if m:
        flag = m.group(1)
        continue
    if line.startswith('#endif'):
        flag = ''
        continue
    m = row_re.match(line)
    if not m:
        continue
    target = None
    if m.group(3) == 'ST_PAGE':
        t = page_re.search(lines[n + 1]) or page_re.search(line)
        target = t.group(1) if t else None
    rows.append(dict(group=m.group(1), label=m.group(2), type=m.group(3), key=m.group(4).strip(), flag=flag, to=target))

leaves = [r for r in rows if r['type'] != 'ST_PAGE']
pages = [r for r in rows if r['type'] == 'ST_PAGE']

base = [l.split('\t') for l in (app / 'tests/settings_baseline.tsv').read_text().splitlines()[1:] if l]
count = collections.Counter(r['label'] for r in leaves)
by_label = {r['label']: r for r in leaves}
for label, typ, key, old, flag in base:
    if count[label] != 1:
        fail('%s appears %d times' % (label, count[label]))
        continue
    r = by_label[label]
    if r['type'] != typ:
        fail('%s changed type %s -> %s' % (label, typ, r['type']))
    if r['key'] != key:
        fail('%s changed config key %s -> %s' % (label, key, r['key']))
    if r['flag'] != flag:
        fail('%s changed build flag %r -> %r' % (label, flag, r['flag']))
extra = set(by_label) - {b[0] for b in base}
if extra:
    fail('settings not in the baseline: %s' % sorted(extra))
print('PASS %d baseline settings each exist exactly once with their type, config key and build flag' % len(base))

parent = {}
for p in pages:
    if p['to'] is None:
        fail('page row %s has no target' % p['label'])
        continue
    if p['to'] in parent:
        fail('page %s is opened from two rows (%s and %s)' % (p['to'], parent[p['to']], p['group']))
    parent[p['to']] = p['group']
    if p['to'] != p['label']:
        fail('page row label %s differs from its page %s' % (p['label'], p['to']))
groups = collections.defaultdict(list)
for r in rows:
    groups[r['group']].append(r)
declared = re.search(r'GROUPS\[\] = \{([^}]*)\}', src).group(1)
if re.findall(r'"([^"]*)"', declared) != TOP:
    fail('top groups changed: %s' % declared)
for g in groups:
    if g not in TOP and g not in parent:
        fail('page %s has rows but nothing opens it' % g)
for t, par in parent.items():
    if t in TOP:
        fail('top group %s is also a sub page' % t)
    if par not in TOP:
        fail('page %s hangs under %s, not under a top group' % (t, par))
    if not groups.get(t):
        fail('page %s is empty' % t)
for g in TOP:
    if not groups.get(g):
        fail('top group %s is empty' % g)
print('PASS every page is reachable from exactly one parent, depth 2 at most, none empty or orphaned')

total = collections.Counter({g: len(v) for g, v in groups.items()})
for g, n in total.items():
    if n > MAX_ROWS:
        fail('page %s has %d rows, limit %d' % (g, n, MAX_ROWS))
print('PASS no page has more than %d rows: %s' % (MAX_ROWS, ', '.join('%s %d' % (g, n) for g, n in sorted(total.items()))))

keys = {r['key'].strip('"') for r in leaves if r['key'] != 'NULL'}
links = []
for f in sorted(app.glob('*.c')):
    if f.name != 'settings.c':
        links += re.findall(r'settings_open_key\("([^"]+)"\)', f.read_text())
for k in links:
    if k not in keys:
        fail('deep link %s does not match a setting' % k)
print('PASS %d deep links resolve to a setting: %s' % (len(links), ' '.join(sorted(set(links)))))

tsv = {l.split('\t')[0] for l in (app / 'i18n/strings.tsv').read_text().splitlines() if l and not l.startswith('#')}
for g in groups:
    if g not in tsv:
        fail('page name %s has no i18n row' % g)
print('PASS every page name has a translation row')

if fails:
    sys.exit(1)
print('COMPLETE settings_tree_test.py')
