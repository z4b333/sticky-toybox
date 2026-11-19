#!/usr/bin/env python3
"""Make a .cpfont tuned for a 1-bit e-paper panel.

CrossInk/CrossPoint's converter renders greyscale and stores 2-bit pixels. On
a black-and-white panel those greys are thresholded, and a stem that sits
half-way between two pixel columns comes out one pixel wider than its
neighbours (Loma at 14 pt: b d h u 3 px, n m i r 2 px).

This renders every glyph grid-fitted for monochrome (FreeType's auto-hinter,
FT_LOAD_TARGET_MONO), so equal stems are equal widths, and writes the same
.cpfont v4 file the device already reads (pixels are 0 or 3). Kerning and
ligature tables are written empty: Toybox does not use them.

  python3 tools/make_cpfont_1bit.py --name Loma --sizes 12,14,16,18 \\
      --regular Loma.otf --bold Loma-Bold.otf --italic Loma-Oblique.otf \\
      --bolditalic Loma-BoldOblique.otf --out out/Loma

Ranges kept: Latin/Greek/Cyrillic and punctuation (what a book uses), Thai,
and the shifted Thai mark forms at U+F700-F71F. Needs freetype-py, fonttools.
"""
import argparse
import math
import os
import struct

import freetype
from fontTools.ttLib import TTFont

RANGES = [(0x0020, 0x024F), (0x02B0, 0x036F), (0x0370, 0x03FF), (0x0400, 0x04FF),
          (0x0E00, 0x0E7F), (0x1E00, 0x1EFF), (0x2000, 0x206F), (0x2070, 0x20CF),
          (0x2100, 0x22FF), (0x2500, 0x257F), (0x25A0, 0x25FF), (0xF700, 0xF71F),
          (0xFB00, 0xFB06), (0xFFFD, 0xFFFD)]
FLAGS = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_FORCE_AUTOHINT


def codepoints(path):
    cmap = TTFont(path).getBestCmap()
    return sorted(cp for cp in cmap if any(a <= cp <= b for a, b in RANGES))


def runs(cps):
    out = []
    for cp in cps:
        if out and out[-1][1] == cp - 1:
            out[-1][1] = cp
        else:
            out.append([cp, cp])
    return out


def style(path, size, cut):
    face = freetype.Face(path)
    face.set_char_size(size << 6, size << 6, 150, 150)
    cps = codepoints(path)
    intervals, glyphs, bits = bytearray(), bytearray(), bytearray()
    n = 0
    for a, b in runs(cps):
        intervals += struct.pack('<III', a, b, n)
        n += b - a + 1
    for cp in cps:
        face.load_char(cp, FLAGS)
        g = face.glyph
        bm = g.bitmap
        w, h = bm.width, bm.rows
        if w > 255 or h > 255:
            raise SystemExit(f'U+{cp:04X} is {w}x{h} at {size} pt: too big for the format')
        buf, pitch = bm.buffer, abs(bm.pitch)
        packed, acc, k = bytearray(), 0, 0
        for y in range(h):
            row = y * pitch if bm.pitch >= 0 else (h - 1 - y) * pitch
            for x in range(w):
                acc = (acc << 2) | (3 if buf[row + (x >> 3)] & (0x80 >> (x & 7)) else 0)
                k += 1
                if k == 4:
                    packed.append(acc)
                    acc, k = 0, 0
        if k:
            packed.append(acc << (2 * (4 - k)))
        adv16 = (g.advance.x * 16) // 64  # hinted advance, 1/16 px
        glyphs += struct.pack('<BBHhhH2xI', w, h, adv16, g.bitmap_left, g.bitmap_top,
                              len(packed), len(bits))
        bits += packed
    m = face.size
    advY = math.ceil(m.height / 64)
    asc = math.ceil(m.ascender / 64)
    desc = math.floor(m.descender / 64)
    if advY > 255:
        raise SystemExit(f'line height {advY} too big at {size} pt')
    data = bytes(intervals + glyphs + bits)  # no kerning, no ligatures
    toc = (cut, len(intervals) // 12, len(cps), advY, asc, desc)
    return toc, data


def write(path, styles):
    header = struct.pack('<8sHHB19s', b'CPFONT\0\0', 4, 1, len(styles), bytes(19))
    offset = 32 + 32 * len(styles)
    tocs, blobs = bytearray(), bytearray()
    for (cut, ni, ng, advY, asc, desc), data in styles:
        tocs += struct.pack('<B3xIIBhhHHBBBI4x', cut, ni, ng, advY, asc, desc, 0, 0, 0, 0, 0,
                            offset)
        offset += len(data)
        blobs += data
    with open(path, 'wb') as f:
        f.write(header + tocs + blobs)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    p.add_argument('--name', required=True)
    p.add_argument('--sizes', required=True)
    p.add_argument('--regular', required=True)
    p.add_argument('--bold')
    p.add_argument('--italic')
    p.add_argument('--bolditalic')
    p.add_argument('--out', required=True)
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    cuts = [(0, a.regular), (1, a.bold), (2, a.italic), (3, a.bolditalic)]
    for size in (int(s) for s in a.sizes.split(',')):
        styles = [style(path, size, cut) for cut, path in cuts if path]
        out = os.path.join(a.out, f'{a.name}_{size}.cpfont')
        write(out, styles)
        print(f'{out}: {os.path.getsize(out):,} bytes, line {styles[0][0][3]} px')


if __name__ == '__main__':
    main()
