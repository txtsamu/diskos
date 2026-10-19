#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extra scripts for user text (titles, artists, albums, lyrics): Thai, Vietnamese and Korean.

Two steps:

  prepare   Make the small source fonts in ui/fonts/intl/ from the full Noto fonts (needs fontTools):
              python3 ui/tools/gen_intl_fonts.py prepare --thai NotoSansThai-Regular.ttf \
                  --latin NotoSans-Regular.ttf --cjk NotoSansCJK-Regular.ttc
            Sources: github.com/notofonts (Noto Sans, Noto Sans Thai), Noto Sans CJK (face "KR"). All SIL OFL 1.1.
  generate  Turn ui/fonts/intl/*.ttf into ui/font_thai_*.c, ui/font_viet_*.c and ui/font_kr_16.c (needs lv_font_conv
            1.5.2 on PATH: npm install -g lv_font_conv@1.5.2):
              python3 ui/tools/gen_intl_fonts.py generate

Thai tone marks: the Disc's text engine draws each character on its own, without the font's mark-stacking rules.
Noto places a tone mark low, where an upper vowel would be, so on top of a vowel (as in "ที่") it disappeared into
it. The prepared Thai font moves tone marks (and thanthakhat) up above the upper vowels instead: they never collide,
and on a bare consonant they simply sit a little higher, as in many simple Thai fonts.
"""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.dirname(HERE)
SRC = os.path.join(UI, "fonts", "intl")

THAI_RANGE = (0x0E01, 0x0E5B)
VIET_RANGE = (0x1EA0, 0x1EF9)          # precomposed Vietnamese letters (the rest are in the existing Latin fonts)
TONE_MARKS = [0x0E48, 0x0E49, 0x0E4A, 0x0E4B, 0x0E4C]
UPPER_VOWELS = [0x0E31, 0x0E34, 0x0E35, 0x0E36, 0x0E37, 0x0E47, 0x0E4D]
SIZES = (14, 16, 18, 20)


def ks_x_1001_hangul():
    """The 2,350 everyday Hangul syllables of KS X 1001 (what EUC-KR can write)."""
    out = []
    for hi in range(0xB0, 0xC9):
        for lo in range(0xA1, 0xFF):
            try:
                out.append(ord(bytes([hi, lo]).decode("euc_kr")))
            except UnicodeDecodeError:
                pass
    return sorted(c for c in set(out) if 0xAC00 <= c <= 0xD7A3)


def subset(src, dst, codepoints, font_number=0):
    from fontTools import subset as fts
    opts = fts.Options()
    opts.hinting = False
    opts.layout_features = []
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    opts.font_number = font_number
    font = fts.load_font(src, opts)
    s = fts.Subsetter(opts)
    s.populate(unicodes=codepoints)
    s.subset(font)
    return font


def raise_thai_marks(font):
    """Move tone marks up so their bottom sits just above the tallest upper vowel."""
    from fontTools.pens.boundsPen import BoundsPen
    gs = font.getGlyphSet()
    cmap = font.getBestCmap()

    def bounds(cp):
        p = BoundsPen(gs)
        gs[cmap[cp]].draw(p)
        return p.bounds                     # (xMin, yMin, xMax, yMax) or None

    vowel_top = max(bounds(cp)[3] for cp in UPPER_VOWELS if cp in cmap and bounds(cp))
    gap = int(font["head"].unitsPerEm * 0.03)
    glyf = font["glyf"]
    top = 0
    for cp in TONE_MARKS:
        if cp not in cmap:
            continue
        g = glyf[cmap[cp]]
        b = bounds(cp)
        if not b:
            continue
        dy = int(vowel_top + gap - b[1])
        if dy <= 0:
            continue
        if g.isComposite():
            for comp in g.components:
                comp.y += dy
        else:
            g.coordinates.translate((0, dy))
        g.recalcBounds(glyf)
        top = max(top, b[3] + dy)
    # keep the font's own ascent above the moved marks so nothing is cut off at the top of a line
    for t, attr in (("hhea", "ascent"), ("OS/2", "usWinAscent"), ("OS/2", "sTypoAscender")):
        if t in font and getattr(font[t], attr) < top:
            setattr(font[t], attr, int(top))
    return vowel_top, top


def prepare(a):
    os.makedirs(SRC, exist_ok=True)
    f = subset(a.thai, None, list(range(THAI_RANGE[0], THAI_RANGE[1] + 1)))
    vt, top = raise_thai_marks(f)
    f.save(os.path.join(SRC, "NotoSansThai-Disco.ttf"))
    print(f"thai: tone marks raised above upper vowels (vowel top {vt}, new mark top {top})")
    f = subset(a.latin, None, list(range(VIET_RANGE[0], VIET_RANGE[1] + 1)))
    f.save(os.path.join(SRC, "NotoSans-Vietnamese.ttf"))
    print("vietnamese: done")
    hangul = ks_x_1001_hangul()
    from fontTools.ttLib import TTCollection
    names = [x["name"].getDebugName(4) for x in TTCollection(a.cjk).fonts]
    idx = names.index("Noto Sans CJK KR")
    f = subset(a.cjk, None, hangul, font_number=idx)
    f.save(os.path.join(SRC, "NotoSansKR-KSX1001.otf"))
    print(f"korean: {len(hangul)} syllables")


def conv(font, size, ranges, name):
    cmd = ["lv_font_conv", "--font", font, "--size", str(size), "--bpp", "4", "--format", "lvgl", "--no-compress",
           "--force-fast-kern-format", "--lv-include", "lvgl/lvgl.h", "-o", os.path.join(UI, name + ".c")]
    for r in ranges:
        cmd += ["-r", r]
    subprocess.run(cmd, check=True)


def generate(_a):
    thai = os.path.join(SRC, "NotoSansThai-Disco.ttf")
    viet = os.path.join(SRC, "NotoSans-Vietnamese.ttf")
    kr = os.path.join(SRC, "NotoSansKR-KSX1001.otf")
    for s in SIZES:
        conv(thai, s, ["0x%04X-0x%04X" % THAI_RANGE], f"font_thai_{s}")
        conv(viet, s, ["0x%04X-0x%04X" % VIET_RANGE], f"font_viet_{s}")
    cps = ks_x_1001_hangul()
    conv(kr, 16, [",".join("0x%04X" % c for c in cps)], "font_kr_16")
    print("generated font_thai_{14,16,18,20}.c, font_viet_{14,16,18,20}.c, font_kr_16.c")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("prepare"); p.add_argument("--thai", required=True); p.add_argument("--latin", required=True)
    p.add_argument("--cjk", required=True); p.set_defaults(fn=prepare)
    sp.add_parser("generate").set_defaults(fn=generate)
    a = ap.parse_args()
    a.fn(a)
