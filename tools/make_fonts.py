#!/usr/bin/env python3
"""Bakes DejaVu Sans into 1-bit proportional bitmap fonts for the panel.

The device used to draw everything with one 8x8 pixel font scaled 2x, 3x and 4x,
which meant every large glyph was a grid of fat squares. These tables are drawn
at the size they are used, with real letterforms and real widths, so text is
sharper and about a third narrower for the same height.

Run from the repo root:  python3 tools/make_fonts.py > src/fonts_ui.h
"""
from PIL import Image, ImageDraw, ImageFont
import sys

FIRST, LAST = 32, 126
# Accented Latin, appended after the ASCII run in every face. Without these,
# an e-acute inside 44 px text dropped to the 32 px international face and
# "resume" read as a ransom note. Latin-1's letters plus the handful of
# Extended-A that western European text actually uses.
EXTRAS = ([c for c in range(0xC0, 0x100)] + [0x152, 0x153, 0x160, 0x161, 0x17D, 0x17E, 0x178]
          # Typographic punctuation: commercial EPUBs use curly quotes, real
          # dashes and the ellipsis on nearly every line, and at 44 px they
          # had the same dropped-glyph problem the accents did.
          + [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026])
# Ink is any pixel darker than this on a white ground, so a higher number pulls
# more of the antialiased edge into the glyph and thickens every stroke without
# changing the typeface or moving a single layout.
#
# 140 was chosen on a monitor. On the panel it reads thin: e-paper renders a
# one-pixel stroke lighter than a backlit screen does. 176 is as far as this can
# go before the counters in a, e and o start to close at 12 px.
THRESH = 176

# ITAL is DejaVu's own oblique, drawn by the type designer rather than sheared
# from the roman by the renderer. On a 1-bit panel at 16 px a shear closes the
# counters and reads as a printing fault; the drawn face keeps its stems.
FACES = {
    'REG': '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
    'BOLD': '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
    'ITAL': '/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf',
}
# name -> pixel height of the line box, one per TSize.
#
# The first set of these was 12/16/24/32, chosen by counting pixels on a
# monitor. On the panel it is 235 DPI, so 16 px of body text is 1.7 mm -- about
# five point, smaller than a medicine label, and the first person to hold the
# device said so immediately. Every size moves up one step: body text is now
# 2.6 mm, roughly seven and a half point, which is ordinary book size.
#
# The smallest is 16 rather than 12 for the same reason it was once 12 rather
# than 8. A face turns to mush below about eleven pixels, and anything under a
# millimetre and a half is decoration rather than text.
SIZES = [('16', 16), ('24', 24), ('32', 32), ('44', 44)]


def fit(path, px):
    """The largest point size whose ascender-to-descender fits the line box.

    Asking for `px` directly overflows: DejaVu at 24 needs 29 rows to hold both,
    so every p, q and y came out with its tail cut off. The box is the line, and
    the whole face has to live inside it.
    """
    best = 6
    for size in range(6, px * 2):
        f = ImageFont.truetype(path, size)
        asc, desc = f.getmetrics()
        if asc + desc <= px:
            best = size
    return best


def build(path, px):
    """Every glyph rasterised into the line box, sharing one baseline."""
    size = fit(path, px)
    font = ImageFont.truetype(path, size)
    asc, desc = font.getmetrics()
    top = (px - (asc + desc)) // 2  # centre whatever slack is left over
    glyphs = []
    for code in list(range(FIRST, LAST + 1)) + EXTRAS:
        ch = chr(code)
        adv = max(int(round(font.getlength(ch))), 1)
        im = Image.new('L', (adv + px, px + px), 255)
        # PIL's default anchor puts the ascender top at the y it is given.
        ImageDraw.Draw(im).text((0, top), ch, font=font, fill=0)
        mask = im.point(lambda v: 255 if v < THRESH else 0)
        rows = []
        for y in range(px):
            rows.append([1 if (y < mask.height and x < mask.width and mask.getpixel((x, y)))
                         else 0 for x in range(adv)])
        glyphs.append((adv, rows))
    return glyphs


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


def emit(name, path, px):
    glyphs = build(path, px)
    data, meta = [], []
    for w, rows in glyphs:
        meta.append((w, len(data)))
        data.extend(pack(rows, w))
    print('const uint8_t FONT_%s_BITS[] PROGMEM = {' % name)
    for i in range(0, len(data), 16):
        print('    ' + ''.join('0x%02X,' % b for b in data[i:i + 16]))
    print('};')
    print('const FontGlyph FONT_%s_GLYPHS[%d] PROGMEM = {' % (name, len(meta)))
    for w, off in meta:
        print('    {%d, %d},' % (w, off))
    print('};')
    print('const UiFont FONT_%s = {%d, FONT_%s_GLYPHS, FONT_%s_BITS};' % (name, px, name, name))
    print()
    return len(data)


def main():
    print('// GENERATED by tools/make_fonts.py -- do not edit by hand.')
    print('//')
    print('// DejaVu Sans, rasterised to 1 bit at the three sizes the UI uses, with')
    print('// real per-glyph widths. Replaces scaling one 8x8 pixel font up to 32 px,')
    print('// which turned every large letter into a grid of squares.')
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
    print('#define UI_HAS_EXTRAS 1')
    # The CrossPoint stand-in family in test/host/fonts_cp.h has no italic, so
    # the renderer asks whether one was baked rather than assuming it.
    print('#define UI_HAS_ITALIC 1')
    print('// Codepoints appended after ASCII index 94, same order in every face.')
    print('constexpr int UI_EXTRA_COUNT = %d;' % len(EXTRAS))
    print('const uint16_t UI_EXTRA_CPS[UI_EXTRA_COUNT] PROGMEM = {')
    print('    ' + ''.join('0x%04X,' % c for c in EXTRAS))
    print('};')
    print()
    total = 0
    for label, px in SIZES:
        for face, path in FACES.items():
            total += emit('%s_%s' % (label, face), path, px)
    print('// %d bytes of glyph data.' % total, file=sys.stderr)


if __name__ == '__main__':
    main()
