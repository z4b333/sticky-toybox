#!/usr/bin/env python3
"""Renders Thai the way the device would, to decide whether it is worth building.

The firmware draws text from a per-glyph bitmap table: one bitmap per codepoint,
an integer advance, a signed left bearing, no shaping engine and no kerning. Thai
needs two things that table does not have today -- codepoints above 127, and
zero-advance combining marks that draw backwards over the base they sit on.

This script models exactly that renderer in Python so the output can be looked at
before any of it is written in C. It deliberately does NOT use HarfBuzz: the
whole question is whether stacked Thai survives a dumb per-glyph blit, so using a
shaper here would prove nothing. A HarfBuzz reference is rendered alongside for
comparison, which is where the compromises show up.

    python3 tools/thai_proof.py            # writes /tmp/thai/*.png
"""
import os
from PIL import Image, ImageDraw, ImageFont

REG = '/usr/share/fonts/opentype/tlwg/Loma.otf'
BOLD = '/usr/share/fonts/opentype/tlwg/Loma-Bold.otf'
THR = 140          # same ink threshold the Latin generator uses
OUT = '/tmp/thai'
SCREEN_W, SCREEN_H = 480, 800

# The four line boxes the UI is built around.
SIZES = [12, 16, 24, 32]


def fit(path, px):
    """Largest point size whose ascender-to-descender fits the line box.

    Same rule as the Latin faces, and it is the whole problem with Thai: Loma
    needs 1.59x em for ascender plus descender against DejaVu's ~1.17, so the
    letterforms that come out of a 16 px box are markedly smaller.
    """
    best = 4
    for size in range(4, px * 3):
        f = ImageFont.truetype(path, size)
        asc, desc = f.getmetrics()
        if asc + desc <= px:
            best = size
    return best


# Thai marks, by where they sit. A tone mark riding an upper vowel is a second
# storey; the same tone mark on a bare consonant belongs one storey lower.
UPPER = set('\u0e31\u0e34\u0e35\u0e36\u0e37\u0e47\u0e4d\u0e4e')
TONE = set('\u0e48\u0e49\u0e4a\u0e4b\u0e4c')


class Face:
    """One baked face: a bitmap, bearing and advance per codepoint.

    Built with layout_engine=BASIC on purpose -- each glyph is rasterised alone,
    at its own bearings, exactly as the device would store it.
    """

    def __init__(self, path, px):
        self.px = px
        self.font = ImageFont.truetype(path, fit(path, px),
                                       layout_engine=ImageFont.Layout.BASIC)
        self.glyphs = {}
        self.tone_drop = self._measure_tone_drop(path)

    def _measure_tone_drop(self, path):
        """How far a lone tone mark has to come down, measured, not guessed.

        A shaping engine lowers a tone mark when there is no upper vowel under
        it. The device has no shaper, so the distance is baked into the face:
        render one cluster both ways and take the difference. Doing it per face
        and per size means it tracks whatever font gets used, instead of a
        constant that happens to suit Loma at 24 px.
        """
        shaped = ImageFont.truetype(path, self.font.size)
        pad = self.px * 3

        def top(font, text):
            im = Image.new('L', (pad * 3, pad * 3), 255)
            ImageDraw.Draw(im).text((pad, pad), text, font=font, fill=0)
            q = im.load()
            ys = [y for y in range(pad * 3) for x in range(pad * 3)
                  if q[x, y] < THR]
            return min(ys) - pad if ys else 0

        # Several samples, because at the smallest boxes one cluster can round
        # to zero while the rest do not.
        deltas = [top(shaped, c) - top(self.font, c)
                  for c in ('\u0e01\u0e48', '\u0e01\u0e49', '\u0e2b\u0e48',
                            '\u0e1b\u0e48')]
        deltas.sort()
        return max(deltas[len(deltas) // 2], 0)

    def glyph(self, ch):
        if ch in self.glyphs:
            return self.glyphs[ch]
        pad = self.px * 3
        im = Image.new('L', (pad * 2, pad * 2), 255)
        ImageDraw.Draw(im).text((pad, pad), ch, font=self.font, fill=0)
        p = im.load()
        cells = [(x, y) for y in range(pad * 2) for x in range(pad * 2)
                 if p[x, y] < THR]
        adv = int(round(self.font.getlength(ch)))
        if not cells:
            g = (adv, 0, 0, 0, 0, [])
        else:
            xs = [c[0] for c in cells]
            ys = [c[1] for c in cells]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            rows = [[1 if p[x, y] < THR else 0 for x in range(x0, x1 + 1)]
                    for y in range(y0, y1 + 1)]
            # left/top are relative to the pen and to the top of the line box.
            g = (adv, x0 - pad, y0 - pad, x1 - x0 + 1, y1 - y0 + 1, rows)
        self.glyphs[ch] = g
        return g

    def width(self, s):
        return sum(self.glyph(c)[0] for c in s)


def draw(img, face, x, y, s):
    """The device blit: advance the pen, stamp each bitmap at its bearing.

    A combining mark has advance 0 and a negative left bearing, so it lands back
    on top of the base that preceded it with no special case in the loop. The one
    rule this renderer does need is the tone-mark drop: every Thai glyph is baked
    at the height it takes when riding an upper vowel, which is correct for
    "\u0e01\u0e35\u0e48" and one storey too high for "\u0e01\u0e48".
    """
    p = img.load()
    prev = ''
    for ch in s:
        adv, left, top, w, h, rows = face.glyph(ch)
        if ch in TONE and prev not in UPPER:
            top += face.tone_drop
        prev = ch
        for ry in range(h):
            for rx in range(w):
                if not rows[ry][rx]:
                    continue
                px_, py_ = x + left + rx, y + top + ry
                if 0 <= px_ < img.width and 0 <= py_ < img.height:
                    p[px_, py_] = 0
        x += adv
    return x


# --- the page ---------------------------------------------------------------

CASES = [
    ('plain consonants',        'กขคงจฉชญฎฐณดตปผฝพฟภมยรลวศษสหฬอฮ'),
    ('upper vowel',             'กิ  กี  กึ  กื  กั  ก็'),
    ('tone mark, no vowel',     'ก่  ก้  ก๊  ก๋  ก์'),
    ('tone over upper vowel',   'กี่  กื้  กั๊  กิ๋'),
    ('vowel below',             'กุ  กู  ญุ  ฐู'),
    ('ascender base',           'ป่  ฟ้  ฬิ  ฝี'),
    ('descender base',          'ญ  ฐ  ฎ  ฏ  ฤ  ฦ'),
    ('words',                   'สวัสดีครับ  ขอบคุณ  น้ำแข็ง'),
    ('a sentence',              'วันนี้อากาศดีมาก ไปเดินเล่นกัน'),
    ('mixed with latin',        'ซื้อนม 2 กล่อง  wifi ต่อแล้ว'),
]

SHOPPING = [
    ('รายการซื้อของ', 24, True),
    ('นม กล่องใหญ่', 16, False),
    ('ขนมปัง', 16, False),
    ('ไข่ไก่', 16, False),
    ('เมล็ดกาแฟ', 16, False),
    ('ก่อนวันศุกร์', 24, True),
    ('จองคิวหมอฟัน', 16, False),
    ('คืนหนังสือห้องสมุด', 16, False),
]


def page_cases():
    """Every awkward cluster Thai can throw at a per-glyph renderer."""
    img = Image.new('L', (SCREEN_W, SCREEN_H), 255)
    faces = {px: Face(REG, px) for px in SIZES}
    label = Face(REG, 12)
    bold24 = Face(BOLD, 24)

    y = 14
    draw(img, bold24, 16, y, 'ตัวอย่างภาษาไทย')
    y += 40
    ImageDraw.Draw(img).line([(16, y), (SCREEN_W - 16, y)], fill=0)
    y += 10

    for name, text in CASES:
        draw(img, label, 16, y, name)
        y += 14
        draw(img, faces[24], 16, y, text)
        y += 34
    return img


def page_sizes():
    """The same line at every box, which is how the size question gets settled."""
    img = Image.new('L', (SCREEN_W, SCREEN_H), 255)
    label = Face(REG, 12)
    y = 16
    for px in SIZES:
        reg, bold = Face(REG, px), Face(BOLD, px)
        draw(img, label, 16, y, 'TS box %d px  (Loma at %d pt)' % (px, reg.font.size))
        y += 16
        draw(img, reg, 16, y, 'สวัสดีครับ วันนี้อากาศดีมาก')
        y += px + 6
        draw(img, bold, 16, y, 'สวัสดีครับ วันนี้อากาศดีมาก')
        y += px + 22
    return img


def page_note():
    """A real screen: the note app, in Thai, with the chrome it actually draws."""
    img = Image.new('L', (SCREEN_W, SCREEN_H), 255)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, SCREEN_W - 1, 38], outline=0)
    d.rectangle([6, 4, 6 + 98, 34], outline=0)
    draw(img, Face(REG, 16), 20, 12, 'HUB')
    draw(img, Face(BOLD, 16), 190, 12, 'รายการ')

    y = 56
    for text, px, heading in SHOPPING:
        f = Face(BOLD if heading else REG, px)
        x = 20
        if not heading:
            d.rectangle([x, y + 2, x + 18, y + 20], outline=0)
            x += 30
        draw(img, f, x, y, text)
        y += px + (18 if heading else 12)
    return img


def reference():
    """What a shaping engine makes of the same strings, for comparison.

    Where this differs from the pages above is the cost of not having one.
    """
    img = Image.new('L', (SCREEN_W, SCREEN_H), 255)
    d = ImageDraw.Draw(img)
    lab = ImageFont.truetype(REG, fit(REG, 12))
    body = ImageFont.truetype(REG, fit(REG, 24))
    y = 14
    d.text((16, y), 'HarfBuzz reference (not the device)', font=lab, fill=0)
    y += 30
    for name, text in CASES:
        d.text((16, y), name, font=lab, fill=0)
        y += 14
        d.text((16, y), text, font=body, fill=0)
        y += 34
    return img.point(lambda v: 0 if v < THR else 255)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in (('cases', page_cases), ('sizes', page_sizes),
                     ('note', page_note), ('reference', reference)):
        fn().save('%s/%s.png' % (OUT, name))
        print('wrote %s/%s.png' % (OUT, name))


main()
