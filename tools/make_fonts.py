#!/usr/bin/env python3
"""Bakes the UI typefaces into 1-bit proportional bitmap fonts for the panel.

Two families, chosen on the device (Settings -> Font): Noto Sans (the default)
and IBM Plex Sans, both under the SIL Open Font License (tools/fonts/). They
replaced DejaVu Sans in v2: narrower, and paired with Thai faces drawn to
match (tools/make_fonts_script.py), where DejaVu stood beside a much smaller
Loma.

Glyphs are rendered grid-fitted for monochrome (FreeType hinting, no
antialiasing) rather than thresholded from greys: a stem that straddles two
pixel columns otherwise comes out a pixel wider than its neighbours, which the
owner saw on glass in a card font rendered the old way.

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
          + [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026]
          # Recipes: a degree sign, footnote marks, and the vulgar fractions a
          # cup measure is written in. A real recipe page opened on the device
          # with a box where its two-thirds should be -- the fractions past
          # Latin-1 are not in any face this carries, and the ones inside it
          # were coming from the international fallback, half a size off the
          # words either side of them.
          + [0x00B0, 0x00B2, 0x00B3, 0x00B9, 0x00BC, 0x00BD, 0x00BE]
          + [0x2153, 0x2154, 0x215B, 0x215C, 0x215D, 0x215E])
# SORTED, and it matters: the renderer binary-searches this table rather than
# scanning it, which is both faster per glyph and the end of a trap. It used to
# scan, behind a hand-written "is it roughly in the Latin range" test -- and the
# first codepoints added outside that range (the fractions above) were rejected
# before the scan and drew a box, with the table itself perfectly correct.
EXTRAS = sorted(set(EXTRAS))
# Ink is any pixel darker than this on a white ground, so a higher number pulls
# more of the antialiased edge into the glyph and thickens every stroke without
# changing the typeface or moving a single layout.
#
# 140 was chosen on a monitor. On the panel it reads thin: e-paper renders a
# one-pixel stroke lighter than a backlit screen does. 176 is as far as this can
# go before the counters in a, e and o start to close at 12 px.
THRESH = 176

# ITAL is each family's own italic, drawn by the type designer rather than
# sheared from the roman by the renderer. BOLD is the SemiBold cut: one step
# heavier, which on a 1-bit panel reads as bold without filling the counters.
HERE = __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), 'fonts')
FAMILIES = [
    # (table prefix, faces) -- Noto keeps the plain FONT_<size>_<style> names,
    # which the CrossPoint stand-in tables (test/host/fonts_cp.h) also define.
    ('', {
        # Medium, not Regular (owner, 19 Nov): Regular's stems read thin on
        # the panel; Medium is one step heavier and still opens its counters.
        'REG': HERE + '/NotoSans-Medium.ttf',
        'BOLD': HERE + '/NotoSans-SemiBold.ttf',
        'ITAL': HERE + '/NotoSans-Italic.ttf',
    }),
    ('PLX_', {
        # Medium for the same reason as Noto (owner, 21 Nov).
        'REG': HERE + '/IBMPlexSans-Medium.ttf',
        'BOLD': HERE + '/IBMPlexSans-SemiBold.ttf',
        'ITAL': HERE + '/IBMPlexSans-Italic.ttf',
    }),
]
# name -> pixel height of the line box, one per TSize.
#
# The first set of these was 12/16/24/32, chosen by counting pixels on a
# monitor. On the panel it is 235 DPI, so 16 px of body text is 1.7 mm -- about
# five point, smaller than a medicine label, and the first person to hold the
# device said so immediately. Every size moves up one step: body text is now
# 2.6 mm, roughly seven and a half point, which is ordinary book size.
#
# The smallest is 18 rather than 16, and was 12 before that, for the same
# reason it was once 12 rather than 8. A face turns to mush below about eleven
# pixels, and anything under a millimetre and a half is decoration rather than
# text -- 16 px is 1.7 mm, which several people read by holding the device
# closer. 18 is 1.9 mm, about six point. The table name keeps the pixel height
# so nothing has to guess which is which.
SIZES = [('20', 20), ('24', 24), ('32', 32), ('44', 44)]
# The smallest moved from 18 to 20 (owner, 28 Nov, after a test build on glass:
# 18 px Medium "barely readable"), and one weight up as well: at 20 px the
# regular face is SemiBold, bold is Bold and italic is SemiBold Italic. A one-
# pixel stem is what made small text break up on the panel; a heavier cut at
# nearly the same width fixed it where two more pixels of height alone did not.
SMALL_PX = 20
SMALL_FACES = {
    '': {'REG': HERE + '/NotoSans-SemiBold.ttf', 'BOLD': HERE + '/NotoSans-Bold.ttf',
         'ITAL': HERE + '/NotoSans-SemiBoldItalic.ttf'},
    'PLX_': {'REG': HERE + '/IBMPlexSans-SemiBold.ttf', 'BOLD': HERE + '/IBMPlexSans-Bold.ttf',
             'ITAL': HERE + '/IBMPlexSans-SemiBoldItalic.ttf'},
}


# What a line box has to hold: the tallest accented capitals Western text uses
# and the deepest descenders. Fitting the INK of these, not the font's declared
# ascender and descender: Noto declares room for stacked Vietnamese accents, and
# fitting that drew every UI word a sixth smaller than the DejaVu it replaced,
# on layouts drawn for DejaVu's size. A stacked Vietnamese capital may now reach
# a pixel or two into the gap above its line, which every layout leaves.
SAMPLE = 'ÅÉÀÊÖgjpqy|()'


def ink_rows(font):
    _, t, _, b = font.getbbox(SAMPLE)
    return t, b


def fit(path, px):
    """The largest point size whose sample ink fits the line box."""
    best = 6
    for size in range(6, px * 2):
        t, b = ink_rows(ImageFont.truetype(path, size))
        if b - t <= px:
            best = size
    return best


def place(font, px):
    """Where to put the ascender top so the sample ink is centred in the box."""
    t, b = ink_rows(font)
    return (px - (b - t)) // 2 - t


def build(path, px):
    """Every glyph rasterised into the line box, sharing one baseline."""
    size = fit(path, px)
    font = ImageFont.truetype(path, size)
    top = place(font, px)
    glyphs = []
    for code in list(range(FIRST, LAST + 1)) + EXTRAS:
        ch = chr(code)
        adv = max(int(round(font.getlength(ch))), 1)
        im = Image.new('L', (adv + px, px + px), 255)
        # PIL's default anchor puts the ascender top at the y it is given.
        # fontmode '1': hinted, monochrome -- every stem on whole pixels.
        d = ImageDraw.Draw(im)
        d.fontmode = '1'
        d.text((0, top), ch, font=font, fill=0)
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
    print('// Noto Sans (FONT_*) and IBM Plex Sans (FONT_PLX_*), SIL OFL 1.1, rasterised')
    print('// to 1 bit, grid-fitted, at the four sizes the UI uses, with real per-glyph')
    print('// widths. Regular, SemiBold (as bold) and Italic.')
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
    print('#define UI_HAS_PLEX 1')
    print('// Codepoints appended after ASCII index 94, same order in every face.')
    print('constexpr int UI_EXTRA_COUNT = %d;' % len(EXTRAS))
    print('const uint16_t UI_EXTRA_CPS[UI_EXTRA_COUNT] PROGMEM = {')
    print('    ' + ''.join('0x%04X,' % c for c in EXTRAS))
    print('};')
    print()
    total = 0
    for prefix, faces in FAMILIES:
        for label, px in SIZES:
            for face, path in faces.items():
                if px == SMALL_PX:
                    path = SMALL_FACES[prefix][face]
                total += emit('%s%s_%s' % (prefix, label, face), path, px)
    print('// %d bytes of glyph data.' % total, file=sys.stderr)


if __name__ == '__main__':
    main()
