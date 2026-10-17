#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
"""Generate theme_presets.inc: the built-in colour presets, each an EXPLICIT dark palette and an EXPLICIT light
palette (nothing is derived at runtime). A preset starts from the default palettes in theme.c, tints the neutral
surface family toward its hue, sets its own accent, then moves foreground colours until every contrast rule below
passes with NO waivers. This file is the preferred form for editing the presets: change PRESETS (or the default
palettes) and rerun it. The release test checks the generated file is current and re-checks every rule.

Usage: python3 tools/gen_theme_presets.py [--check]    (--check: exit 1 if theme_presets.inc is stale)
"""
import colorsys
import re
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent.parent          # the UI source directory (theme.c lives here)
OUT = APP / 'theme_presets.inc'

# name, hue (degrees) for the surface tint, tint strength dark/light, accent dark, accent light
PRESETS = []   # recolour presets were dropped (owner, 2026-09-26): every theme is a hand-designed language (EXPLICIT)
# Stone retains its existing explicit palette; hidden legacy palettes are retired.
EXPLICIT = []  # Hidden legacy themes retired; shared base palettes remain.



def seed(bg, surf, rais, strong, txt, sec, muted, acc, on_acc, border, texture, decor1, decor2, key_sel, knob=None,
         selected=None, connected=None, track=None, placeholder=None, sw_knob=None, sw_off=None):
    """A full palette override from a theme's few design colours (the rest follow from them, then get fitted)."""
    return dict(CANVAS=bg, SURFACE=surf, SURFACE_RAISED=rais, SURFACE_STRONG=strong,
                SURFACE_SELECTED=selected if selected is not None else rais, ART_PLACEHOLDER=rais,
                LIST_PRESSED=rais, SURFACE_PRESSED=rais, RAISED_PRESSED=strong, BORDER=border, BORDER_STRONG=strong,
                CONNECTED_SURFACE=connected if connected is not None else rais,
                CONTROL_TRACK=track if track is not None else strong, CONTROL_TRACK_STRONG=track if track is not None else strong,
                CONTROL_FILL=acc, CONTROL_KNOB=knob if knob is not None else txt, CONTROL_OFF=rais, GRABBER=muted,
                PAGE_DOT_ACTIVE=txt, PAGE_DOT_INACTIVE=strong, PAGE_DOT_DIM=border, KEY_SURFACE=rais,
                KEY_PRESSED=strong, KEY_SELECTED=key_sel, KEY_TEXT=txt, TEXT_PRIMARY=txt, TEXT_SECONDARY=sec,
                TEXT_MUTED=muted, ACCENT_PRIMARY=acc, ACCENT_EMPHASIS=acc, ACCENT_IDLE=acc, ON_ACCENT=on_acc,
                SWITCH_ON=acc, SWITCH_OFF=sw_off if sw_off is not None else strong, SWITCH_KNOB=sw_knob if sw_knob is not None else txt, ACTION_SURFACE=acc, WIDGET_PRIMARY=acc, SIGNAL_ACTIVE=txt,
                SIGNAL_INACTIVE=muted, DOT_GRID=texture, DECOR_1=decor1, DECOR_2=decor2, IMAGE_TINT=bg,
                INPUT_PLACEHOLDER=muted, FOLDER_ICON=sec, ART_WALL_FRONT=rais, ART_WALL_SIDE=surf)


# Stone remains visible at its original saved ID 6. Keep its palette unchanged.
EXPLICIT += [
    ('stone', {   # soft and tactile: raised rounded surfaces, mint / terracotta
        'dark': seed(0x1C1F22, 0x2A2F34, 0x353B41, 0x40474E, 0xEEF1F4, 0xC7CED6, 0xA3ADB8, 0x7FE0B0, 0x0E2A1E,
                     0x2A2F34, 0x111315, 0x7FE0B0, 0x111315, 0x2D5A45, sw_knob=0x1C1F22, sw_off=0xA3ADB8),
        'light': seed(0xEFEDE8, 0xFFFFFF, 0xE4E1DA, 0xD6D2C9, 0x2B2F36, 0x4B5260, 0x6B7280, 0xD2603F, 0xFFFFFF,
                      0xE4E1DA, 0xD6D2C9, 0xD2603F, 0xD6D2C9, 0xF4D3C8),
    }),
]


# neutral roles that take the preset's tint (backgrounds, surfaces, tracks, keys)
TINTED = {'CANVAS', 'SURFACE', 'SURFACE_RAISED', 'SURFACE_STRONG', 'SURFACE_SELECTED', 'ART_PLACEHOLDER',
          'LIST_PRESSED', 'SURFACE_PRESSED', 'RAISED_PRESSED', 'BORDER', 'BORDER_STRONG', 'CONTROL_TRACK',
          'CONTROL_TRACK_STRONG', 'CONTROL_OFF', 'KEY_SURFACE', 'KEY_PRESSED', 'SWITCH_OFF', 'ART_WALL_FRONT',
          'ART_WALL_SIDE', 'TEXT_SEPARATOR', 'PAGE_DOT_INACTIVE', 'PAGE_DOT_DIM', 'GRABBER', 'ACTION_SHADOW',
          'WIDGET_TRACK'}
ACCENT_ROLES = ('ACCENT_PRIMARY', 'ACCENT_EMPHASIS', 'SWITCH_ON', 'ACTION_SURFACE', 'WIDGET_PRIMARY')


def lum(x):
    def ch(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * ch(x >> 16 & 255) + 0.7152 * ch(x >> 8 & 255) + 0.0722 * ch(x & 255)


def ratio(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def rules():
    """(foreground, background, minimum ratio). A background 'ROLE@pct/UNDER' is ROLE at pct% alpha over UNDER.
    The release test (test_ae) holds the same table and fails if the two differ."""
    r = []
    for fg in ('TEXT_PRIMARY', 'TEXT_SECONDARY', 'TEXT_MUTED'):
        r += [(fg, bg, 4.5) for bg in ('CANVAS', 'SURFACE', 'SURFACE_RAISED')]
    for fg in ('TEXT_DISABLED', 'TEXT_FOOTNOTE', 'TEXT_HINT', 'TEXT_SEPARATOR'):
        r += [(fg, bg, 3.0) for bg in ('CANVAS', 'SURFACE')]
    for fg in ('STATUS_INFO', 'STATUS_SUCCESS', 'STATUS_DANGER', 'STATUS_WARNING', 'ACCENT_PRIMARY'):
        r += [(fg, bg, 3.0) for bg in ('CANVAS', 'SURFACE', 'SURFACE_RAISED')]
    r += [('TEXT_PRIMARY', 'SURFACE_SELECTED', 4.5), ('TEXT_PRIMARY', 'CONNECTED_SURFACE', 4.5),
          ('TEXT_PRIMARY', 'SURFACE_STRONG', 4.5), ('KEY_TEXT', 'KEY_SURFACE', 4.5), ('KEY_TEXT', 'KEY_SELECTED', 4.5),
          ('KEY_TEXT', 'KEY_PRESSED', 4.5), ('STATUS_DANGER', 'DANGER_SURFACE', 4.5),
          ('STATUS_DANGER', 'DANGER_BUTTON_SURFACE', 4.5), ('STATUS_DANGER', 'DANGER_SURFACE_PRESSED', 4.5),
          ('ON_ACCENT', 'ACCENT_PRIMARY', 3.0), ('ON_ACCENT', 'ACCENT_EMPHASIS', 3.0),
          ('CONTROL_FILL', 'CONTROL_TRACK', 3.0), ('CONTROL_KNOB', 'CONTROL_TRACK', 3.0),
          ('ACCENT_PRIMARY', 'CONTROL_TRACK', 3.0), ('ON_CONTROL', 'CONTROL_FILL', 3.0),
          ('PAGE_DOT_ACTIVE', 'CANVAS', 3.0), ('OUTLINE_BRIGHT', 'CANVAS', 3.0), ('TEXT_PRIMARY', 'CONTROL_OFF', 3.0),
          ('FOLDER_ICON', 'CANVAS', 3.0), ('GRABBER', 'SURFACE', 3.0), ('SWITCH_OFF', 'SURFACE', 3.0),
          ('SWITCH_ON', 'SURFACE', 3.0), ('INPUT_PLACEHOLDER', 'SURFACE', 4.5), ('INPUT_CURSOR', 'SURFACE', 3.0),
          ('ON_ACTION', 'ACTION_SURFACE', 4.5), ('SIGNAL_ACTIVE', 'SURFACE', 3.0),
          ('SWITCH_KNOB', 'SWITCH_OFF', 3.0), ('SWITCH_KNOB', 'SWITCH_ON', 3.0),
          ('SIGNAL_ACTIVE', 'SURFACE@50/CANVAS', 3.0), ('SIGNAL_INACTIVE', 'SURFACE@50/CANVAS', 3.0),
          ('SIGNAL_ACTIVE', 'CONNECTED_SURFACE', 3.0), ('SIGNAL_INACTIVE', 'CONNECTED_SURFACE', 3.0),
          ('TEXT_PRIMARY', 'SURFACE@50/CANVAS', 4.5)]
    return r


def bgval(p, bg):
    if '@' not in bg:
        return p[bg]
    top, rest = bg.split('@')
    pct, under = rest.split('/')
    a = int(pct) / 100.0
    return sum(round((p[top] >> sh & 255) * a + (p[under] >> sh & 255) * (1 - a)) << sh for sh in (16, 8, 0))


def palette(src, name):
    b = src[src.index(name + '[THEME_CLR_COUNT]'):]
    b = b[:b.index('};')]
    return {k: int(v, 16) for k, v in re.findall(r'\[THEME_CLR_([A-Z0-9_]+)\]\s*=\s*0x([0-9A-Fa-f]{6})', b)}


def tint(rgb, hue, strength):
    """Keep the neutral's lightness; give it the preset hue at a saturation that grows with `strength`."""
    r, g, b = (rgb >> 16 & 255) / 255, (rgb >> 8 & 255) / 255, (rgb & 255) / 255
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    r, g, b = colorsys.hls_to_rgb(hue / 360.0, l, min(1.0, s + strength))
    return (round(r * 255) << 16) | (round(g * 255) << 8) | round(b * 255)


def toward(rgb, target, k):
    """rgb moved k steps of 4% toward target (0x000000 or 0xFFFFFF), channel-wise."""
    out = 0
    for sh in (16, 8, 0):
        c, t = rgb >> sh & 255, target >> sh & 255
        c = t + (c - t) * (0.96 ** k)
        out |= min(255, max(0, round(c))) << sh
    return out


def failing(p):
    return [(fg, bg, mn) for fg, bg, mn in rules() if ratio(p[fg], bgval(p, bg)) + 1e-9 < mn]


def fit(p):
    """Deterministic: for each foreground that fails, pick the SMALLEST move (k steps toward white or black) that
    makes all of ITS rules pass, holding everything else. Repeat over the failing foregrounds in a fixed order until
    nothing fails; a foreground that cannot be satisfied either way, or a cycle, fails loudly."""
    for _ in range(len(p) + 1):
        bad = failing(p)
        if not bad:
            return p
        fg = sorted({f for f, _, _ in bad})[0]
        mine = [(f, b, m) for f, b, m in rules() if f == fg]
        best = None
        for k in range(1, 120):
            for target in (0xFFFFFF, 0x000000):
                cand = toward(p[fg], target, k)
                if all(ratio(cand, bgval(p, b)) + 1e-9 >= m for _, b, m in mine):
                    best = cand
                    break
            if best is not None:
                break
        if best is None:
            raise SystemExit('cannot fit %s against %s' % (fg, [b for _, b, _ in mine]))
        p[fg] = best
    raise SystemExit('fit did not settle: %s' % failing(p))


def build(src):
    base = {'dark': palette(src, 'DEFAULT_DARK'), 'light': palette(src, 'DEFAULT_LIGHT')}
    out = []
    for name, hue, sd, sl, acc_d, acc_l in PRESETS:
        pals = {}
        for var, strength, acc in (('dark', sd, acc_d), ('light', sl, acc_l)):
            p = dict(base[var])
            for role in TINTED:
                p[role] = tint(p[role], hue, strength)
            for role in ACCENT_ROLES:
                p[role] = acc
            if var == 'dark':
                p['ON_ACCENT'] = 0x000000     # dark presets use bright accents: black glyphs on them
            pals[var] = fit(p)
            pals[var]['USAGE_CHARGE_BAND'] = pals[var]['STATUS_SUCCESS']
            pals[var]['USAGE_BAND'] = pals[var]['SURFACE_STRONG']

        out.append((name, pals))
    for name, over in EXPLICIT:
        pals = {}
        for var in ('dark', 'light'):
            p = dict(base[var])
            unknown = set(over[var]) - set(p)
            if unknown:
                raise SystemExit('%s %s: unknown roles %s' % (name, var, sorted(unknown)))
            p.update(over[var])
            pals[var] = fit(p)
            pals[var]['VOLUME_TRACK'] = pals[var]['CONTROL_TRACK_STRONG']
            pals[var]['VOLUME_BADGE'] = pals[var]['SURFACE']
            pals[var]['VOLUME_TICK_MAJOR'] = pals[var]['TEXT_PRIMARY']
            pals[var]['VOLUME_TICK_MINOR'] = pals[var]['TEXT_DISABLED']
            pals[var]['VOLUME_DIGITS'] = pals[var]['TEXT_PRIMARY']
            pals[var]['VOLUME_CAPTION'] = pals[var]['TEXT_SECONDARY']
            pals[var]['PAGE_DOT_QUIET'] = pals[var]['PAGE_DOT_INACTIVE']
            pals[var]['SAVER_TITLE'] = pals[var]['TEXT_PRIMARY']
            pals[var]['SAVER_SUBTITLE'] = pals[var]['TEXT_SECONDARY']
            pals[var]['SAVER_ARTIST'] = pals[var]['TEXT_MUTED']
            pals[var]['SAVER_RING'] = pals[var]['CONTROL_TRACK']
            pals[var]['SAVER_DISC'] = pals[var]['ART_PLACEHOLDER']
            pals[var]['EQ_ZERO'] = pals[var]['BORDER']
            pals[var]['EQ_SLOT'] = pals[var]['CONTROL_TRACK']
            pals[var]['EQ_CAP_DISABLED'] = pals[var]['TEXT_DISABLED']
            pals[var]['EQ_FADER'] = pals[var]['CONTROL_FILL']
            pals[var]['EQ_FADER_DISABLED'] = pals[var]['CONTROL_FILL_MUTED']
            pals[var]['EQ_READONLY'] = pals[var]['TEXT_DISABLED']
            pals[var]['EQ_CHIP'] = pals[var]['SURFACE_RAISED']
            pals[var]['BOOK_HEARD'] = pals[var]['CONTROL_FILL_MUTED']
            pals[var]['WEATHER_SUN'] = pals[var]['STATUS_WARNING']
            pals[var]['WEATHER_MOON'] = pals[var]['TEXT_SECONDARY']
            pals[var]['WEATHER_RAIN'] = pals[var]['STATUS_INFO']
            pals[var]['WEATHER_BORDER'] = pals[var]['BORDER']
            pals[var]['TOAST_SURFACE'] = pals[var]['SURFACE_RAISED']
            pals[var]['TOAST_BORDER'] = pals[var]['BORDER']
            pals[var]['TOAST_ORB'] = pals[var]['TEXT_DISABLED']
            pals[var]['ORBIT_PRESSED'] = pals[var]['SURFACE_PRESSED']
            pals[var]['ORBIT_SHADOW'] = pals[var]['ACTION_SHADOW']
            pals[var]['ORBIT_POINTER'] = pals[var]['GRABBER']
            pals[var]['ORBIT_HUB'] = pals[var]['SURFACE_STRONG']
            pals[var]['USAGE_GRID'] = pals[var]['BORDER']
            pals[var]['USAGE_TEXT'] = pals[var]['TEXT_PRIMARY']
            pals[var]['USAGE_AMBER'] = pals[var]['STATUS_WARNING']
            pals[var]['USAGE_FAINT'] = pals[var]['BORDER']
            pals[var]['QUEUE_DRAG'] = pals[var]['SURFACE_SELECTED']
            pals[var]['QUEUE_HANDLE'] = pals[var]['TEXT_DISABLED']
            pals[var]['PRIMARY_PRESSED'] = pals[var]['ACCENT_EMPHASIS']
            pals[var]['SWITCH_SELECTED'] = pals[var]['SWITCH_ON']
            pals[var]['DANGER_DIALOG'] = pals[var]['DANGER_BUTTON_SURFACE']
            pals[var]['POSTER_SUBTITLE'] = pals[var]['TEXT_SECONDARY']
            pals[var]['POSTER_SIDE'] = pals[var]['TEXT_PRIMARY']

            pals[var]['USAGE_CHARGE_BAND'] = pals[var]['STATUS_SUCCESS']
            pals[var]['USAGE_BAND'] = pals[var]['SURFACE_STRONG']

        out.append((name, pals))                        # hand-designed presets, in EXPLICIT order
    return base, out


def render(base, presets):
    order = list(base['dark'].keys())
    lines = ['/* SPDX-License-Identifier: GPL-3.0-or-later */', '/* Copyright (C) 2026 diskOS contributors */', '/* GENERATED by tools/gen_theme_presets.py - edit that script, not this file. Built-in colour presets: each an',
             ' * explicit dark and light palette, every one passing the release contrast rules with no waivers. */']
    for name, pals in presets:
        ident = name.upper().replace('-', '_')
        for var in ('dark', 'light'):
            lines.append('static const uint32_t PRESET_%s_%s[THEME_CLR_COUNT] = {' % (ident, var.upper()))
            for role in order:
                lines.append('    %-35s= 0x%06X,' % ('[THEME_CLR_%s]' % role, pals[var][role]))
            lines.append('};')
    lines.append('#define THEME_PRESETS_GENERATED \\')
    lines.append(' \\\n'.join('    [6] = { "%s", PRESET_%s_DARK, PRESET_%s_LIGHT },' % (n, n.upper().replace('-', '_'), n.upper().replace('-', '_'))
                              for n, _ in presets))
    lines.append('#define THEME_PRESET_NAMES_GENERATED ' + ', '.join('[6] = "%s"' % n for n, _ in presets))
    return '\n'.join(lines) + '\n'


def main():
    text = render(*build((APP / 'theme.c').read_text()))
    if '--check' in sys.argv:
        sys.exit(0 if OUT.exists() and OUT.read_text() == text else 1)
    OUT.write_text(text)
    print('wrote', OUT)


if __name__ == '__main__':
    main()
