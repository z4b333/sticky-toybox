#!/usr/bin/env python3
"""Re-bakes CrossPoint's four UI faces into the Sticky's UiFont tables.

This exists so the host preview harness can render every Toybox screen with the
fonts the CrossPoint port would actually use, rather than the ones the Sticky
firmware carries. Toybox's layouts assume line boxes of 16 / 24 / 32 / 44 px;
CrossPoint's UI faces are 23 / 24 / 29 / 51, so the question "does the port
still fit on the panel" can only be answered by looking at it.

The output is drop-in compatible with src/fonts_ui.h -- same struct names, same
symbol names -- so gfx.cpp compiles against either one unchanged.

Two fidelity notes. CrossPoint kerns pairs at draw time and this does not, so
strings here are a pixel or two WIDER than on the device: the overflow report
errs toward false alarms, never toward missing one. And CrossPoint's small face
ships no bold cut, which is reproduced here rather than papered over -- if bold
small text looks unemphasised in the preview, that is the port, not the tool.

Run from the repo root:
    python3 tools/make_fonts_cp.py > test/host/fonts_cp.h
"""
import re
import sys
import zlib

BUILTIN = '/root/crosspoint-reader/lib/EpdFont/builtinFonts'
FIRST, LAST = 32, 126

# Toybox bucket -> (CrossPoint regular face, bold face or None).
# This is the mapping in src/activities/toybox/ToolsCanvasCp.h::fontOf.
BUCKETS = [
    ('16', 'notosans_8_regular', None),                    # TS_SMALL  -> SMALL_FONT_ID
    ('24', 'ubuntu_10_regular', 'ubuntu_10_bold'),         # TS_MED    -> UI_10_FONT_ID
    ('32', 'ubuntu_12_regular', 'ubuntu_12_bold'),         # TS_LARGE  -> UI_12_FONT_ID
    ('44', 'notosans_18_regular', 'notosans_18_bold'),     # TS_HUGE   -> NOTOSANS_18_FONT_ID
]


def ints(text):
    return [int(t, 0) for t in re.findall(r'-?\b(?:0[xX][0-9a-fA-F]+|\d+)\b', text)]


def rows_of(text):
    """The {...} rows of a C array initialiser, each as a list of ints."""
    return [ints(r) for r in re.findall(r'\{([^{}]*)\}', text)]


def block(src, decl, name):
    m = re.search(r'static const %s %s\w*\[\d*\]\s*=\s*\{(.*?)\n\};' % (decl, name), src, re.S)
    return m.group(1) if m else None


def load(name):
    src = open('%s/%s.h' % (BUILTIN, name)).read()

    bitmap = ints(block(src, 'uint8_t', name + 'Bitmaps'))
    glyphs = rows_of(block(src, 'EpdGlyph', name + 'Glyphs'))
    intervals = rows_of(block(src, 'EpdUnicodeInterval', name + 'Intervals'))

    body = re.search(r'static const EpdFontData %s = \{(.*?)\n\};' % name, src, re.S).group(1)
    fields = [f.strip().rstrip(',') for f in body.strip().split('\n')]
    advanceY, ascender = int(fields[4]), int(fields[5])
    is2bit = fields[7] == 'true'

    groups = None
    if fields[8] != 'nullptr':
        groups = rows_of(block(src, 'EpdFontGroup', name + 'Groups'))

    return dict(bitmap=bitmap, glyphs=glyphs, intervals=intervals,
                advanceY=advanceY, ascender=ascender, is2bit=is2bit, groups=groups)


def glyph_index(f, cp):
    for first, last, offset in f['intervals']:
        if first <= cp <= last:
            return offset + cp - first
    return None


def inflate(f, gi):
    """Decompress the group holding glyph gi, and say where in it the glyph starts.

    Compressed groups store each glyph row-byte-aligned (stride = ceil(w/4)),
    which is what GfxRenderer's decompressor compacts away at draw time. Reading
    the aligned form directly is simpler and identical in content.
    """
    for index, (coff, csize, usize, count, first) in enumerate(f['groups']):
        if first <= gi < first + count:
            raw = bytes(f['bitmap'][coff:coff + csize])
            data = zlib.decompressobj(-15).decompress(raw, usize)
            offset = 0
            for i in range(first, gi):
                w, h = f['glyphs'][i][0], f['glyphs'][i][1]
                if w and h:
                    offset += ((w + 3) // 4) * h
            return data, offset
    return None, 0


def ink(f, gi):
    """The glyph's ink as a list of rows of 0/1, in its own bitmap's coordinates."""
    w, h = f['glyphs'][gi][0], f['glyphs'][gi][1]
    if not w or not h:
        return []

    if f['groups']:
        data, offset = inflate(f, gi)
        stride = (w + 3) // 4
        out = []
        for y in range(h):
            row = []
            for x in range(w):
                byte = data[offset + y * stride + (x >> 2)]
                # 0 = white, 3 = black; GfxRenderer paints anything non-white
                # black in BW mode, which is the only mode Toybox draws in.
                row.append(1 if ((byte >> ((3 - (x & 3)) * 2)) & 3) else 0)
            out.append(row)
        return out

    # Uncompressed 1-bit: one continuous MSB-first bitstream, no row padding.
    base = f['glyphs'][gi][6]
    out = []
    pos = 0
    for y in range(h):
        row = []
        for x in range(w):
            byte = f['bitmap'][base + (pos >> 3)]
            row.append((byte >> (7 - (pos & 7))) & 1)
            pos += 1
        out.append(row)
    return out


def cell(f, cp):
    """One glyph re-drawn into a full line box, the shape UiFont stores.

    CrossPoint carries a bearing per glyph and draws from the baseline; the
    Sticky's tables are flush cells. Placing the ink at (left, ascender - top)
    inside a box advanceY tall reproduces the device exactly.
    """
    gi = glyph_index(f, cp)
    if gi is None:
        gi = glyph_index(f, ord('?'))
    g = f['glyphs'][gi]
    w, h, adv, left, top = g[0], g[1], g[2], g[3], g[4]

    advance = max(int(round(adv / 16.0)), 1)
    box = max(advance, left + w)  # a glyph that overhangs its advance still draws
    rows = [[0] * box for _ in range(f['advanceY'])]

    for y, line in enumerate(ink(f, gi)):
        ty = f['ascender'] - top + y
        if not (0 <= ty < f['advanceY']):
            continue
        for x, bit in enumerate(line):
            tx = left + x
            if bit and 0 <= tx < box:
                rows[ty][tx] = 1
    return advance, box, rows


def pack(rows, w):
    out = []
    stride = (w + 7) // 8
    for bits in rows:
        for b in range(stride):
            v = 0
            for i in range(8):
                x = b * 8 + i
                if x < w and bits[x]:
                    v |= 0x80 >> i
            out.append(v)
    return out


def emit(symbol, face):
    f = load(face)
    data, meta = [], []
    for cp in range(FIRST, LAST + 1):
        advance, box, rows = cell(f, cp)
        # The stored column count has to be the box, or a glyph that overhangs
        # its advance would be clipped; the advance is what textWidth sums.
        meta.append((box, len(data)))
        data.extend(pack(rows, box))

    print('const uint8_t FONT_%s_BITS[] PROGMEM = {' % symbol)
    for i in range(0, len(data), 16):
        print('    ' + ''.join('0x%02X,' % b for b in data[i:i + 16]))
    print('};')
    print('const FontGlyph FONT_%s_GLYPHS[%d] PROGMEM = {' % (symbol, len(meta)))
    for w, off in meta:
        print('    {%d, %d},' % (w, off))
    print('};')
    print('const UiFont FONT_%s = {%d, FONT_%s_GLYPHS, FONT_%s_BITS};'
          % (symbol, f['advanceY'], symbol, symbol))
    print()
    return len(data)


def main():
    print('// GENERATED by tools/make_fonts_cp.py -- do not edit by hand.')
    print('//')
    print("// CrossPoint's four UI faces in the Sticky's table format, so the preview")
    print('// harness can render every screen the way the CrossPoint port would.')
    print('// Heights are 23 / 24 / 29 / 51 against the 16 / 24 / 32 / 44 the layouts')
    print('// were drawn for -- which is the whole reason to look.')
    print('#pragma once')
    print('#include <pgmspace.h>')
    print('#include <stdint.h>')
    print()
    print('struct FontGlyph {')
    print('  uint8_t width;   // advance, and the number of columns stored')
    print('  uint16_t offset; // into the size\'s bit table')
    print('};')
    print()
    print('struct UiFont {')
    print('  uint8_t height;  // line box, and the number of rows per glyph')
    print('  const FontGlyph* glyphs;')
    print('  const uint8_t* bits;')
    print('};')
    print()
    total = 0
    for label, regular, bold in BUCKETS:
        total += emit('%s_REG' % label, regular)
        # No bold cut on the small face: CrossPoint falls back to regular, and so
        # does this, rather than hiding the difference behind a synthetic bold.
        total += emit('%s_BOLD' % label, bold or regular)
    print('// %d bytes of glyph data.' % total, file=sys.stderr)


main()
