#!/usr/bin/env python3
"""Bakes each UI family's own Latin and Thai, past what fonts_ui.h carries.

fonts_ui.h holds ASCII plus a few dozen Western accents per family, in fixed
line-box rows. Everything else a European, Vietnamese or Thai sentence needs
lives here, per family and at exactly the UI's four sizes, so a Thai word sits
at the size and on the baseline of the Latin beside it:

  * Latin  -- Latin-1, Extended-A and the Vietnamese Extended Additional block,
              plus the euro, won and dong signs, from the family's Latin face.
  * Thai   -- the full block, from the family's Thai Looped face, at the same
              point size as its Latin (the two are drawn as a pair).

Families: Noto Sans + Noto Sans Thai Looped, and IBM Plex Sans + IBM Plex Sans
Thai Looped (SIL OFL 1.1, tools/fonts/). Glyphs are rendered grid-fitted for a
1-bit panel (FreeType hinting, no antialiasing).

Same record and compression as the shared CJK tables (fonts_intl.h,
tools/intl_zip.py). A face's toneDrop packs three measurements, each taken
against the font's own shaping (HarfBuzz via raqm), not guessed: bits 0-4 how
far a tone mark comes down over a bare consonant, bits 5-9 how far it goes up
over an upper vowel, bits 10-15 how far a mark moves left over a tall
consonant (ป ฝ ฟ ฬ). The old faces (Loma) only ever used the first.

Run from the repo root:
    python3 tools/make_fonts_script.py
writes src/fonts_script.h and src/fonts_script.cpp.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import intl_zip  # noqa: E402

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts')
FAMILIES = [
    ('NOTO', HERE + '/NotoSans-Regular.ttf', HERE + '/NotoSansThaiLooped-Regular.ttf'),
    # Prompt draws its own Latin and Thai (owner, 28 Nov), so one file serves both.
    ('PLX', HERE + '/Prompt-Medium.ttf', HERE + '/Prompt-Medium.ttf'),
]
SIZES = [20, 24, 32, 44]  # 18 -> 20 with the UI (owner, 28 Nov)
BASIC = ImageFont.Layout.BASIC


def rng(a, b):
    return list(range(a, b + 1))


LATIN = rng(0xA1, 0xFF) + rng(0x100, 0x17F) + rng(0x1E00, 0x1EFF) + [
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x20AC, 0x20A9, 0x20AB]
THAI = rng(0x0E01, 0x0E3A) + rng(0x0E3F, 0x0E5B)
TALL = 'ปฝฟฬ'


from make_fonts import fit, place  # noqa: E402 -- the same size and baseline as fonts_ui.h


def ink(font, text, pad):
    """Mono ink of `text` drawn with its ascender top at (pad, pad)."""
    im = Image.new('L', (pad * 4, pad * 4), 255)
    d = ImageDraw.Draw(im)
    d.fontmode = '1'
    d.text((pad, pad), text, font=font, fill=0)
    return im


def bake(font, cps, dy, pad):
    """codepoint -> (adv, left, top, w, h, packed rows)"""
    out = {}
    for cp in cps:
        ch = chr(cp)
        im = ink(font, ch, pad)
        box = Image.eval(im, lambda v: 255 - v).getbbox()
        adv = max(int(round(font.getlength(ch))), 0)
        if not box:
            out[cp] = (adv, 0, 0, 0, 0, b'')
            continue
        x0, y0, x1, y1 = box
        w, h = x1 - x0, y1 - y0
        q = im.load()
        stride = (w + 7) // 8
        packed = bytearray(stride * h)
        for y in range(h):
            for x in range(w):
                if q[x0 + x, y0 + y] < 128:
                    packed[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
        out[cp] = (adv, x0 - pad, y0 - pad + dy, w, h, bytes(packed))
    return out


def has_glyph(path):
    from fontTools.ttLib import TTFont
    return set(TTFont(path, lazy=True).getBestCmap().keys())


def mark_ink(font, base, mark, pad):
    """The mark's own pixels in base+mark: what is inked there but not in base."""
    a = ink(font, base, pad).load()
    b = ink(font, base + mark, pad).load()
    pts = [(x, y) for y in range(pad * 4) for x in range(pad * 4) if b[x, y] < 128 and a[x, y] >= 128]
    return pts


def measure(thai, pt, pad):
    """(drop, raise, tall shift), from the face's own shaping (HarfBuzz).

    drop  -- how far a tone mark comes down over a consonant with no upper
             vowel (Loma-style faces bake tones high).
    raise -- how far a tone mark goes up over an upper vowel (Noto and Plex
             bake tones low, sitting on the consonant).
    shift -- how far a mark over a tall consonant (ป ฝ ฟ ฬ) moves left.
    """
    shaped = ImageFont.truetype(thai, pt)
    basic = ImageFont.truetype(thai, pt, layout_engine=BASIC)

    def top(f, base, mark):
        p = mark_ink(f, base, mark, pad)
        return min(y for _, y in p) if p else None

    def med(v):
        v = sorted(x for x in v if x is not None)
        return max(v[len(v) // 2], 0) if v else 0

    drops, raises, shifts = [], [], []
    for c in ('ก', 'ห', 'น'):
        for t in ('่', '้'):
            s, b = top(shaped, c, t), top(basic, c, t)
            if s is not None and b is not None:
                drops.append(s - b)
            # The same tone, over an upper vowel, shaped -- against where the
            # face draws it unshaped (over the bare consonant).
            for v in ('ี', 'ิ'):
                sv = top(shaped, c + v, t)
                if sv is not None and b is not None:
                    raises.append(b - sv)
    for base in TALL[:3]:
        for m in ('ิ', '่'):
            s = mark_ink(shaped, base, m, pad)
            b = mark_ink(basic, base, m, pad)
            if s and b:
                shifts.append(min(x for x, _ in b) - min(x for x, _ in s))
    return min(med(drops), 31), min(med(raises), 31), min(med(shifts), 63)


def build(latin, thai, box):
    pt = fit(latin, box)
    lf = ImageFont.truetype(latin, pt, layout_engine=BASIC)
    tf = ImageFont.truetype(thai, pt, layout_engine=BASIC)
    la, _ = lf.getmetrics()
    ta, _ = tf.getmetrics()
    top = place(lf, box)  # where fonts_ui.h puts the ascender
    pad = box * 2
    glyphs = {}
    lcmap, tcmap = has_glyph(latin), has_glyph(thai)
    glyphs.update(bake(lf, [c for c in LATIN if c in lcmap], top, pad))
    # The Thai face's baseline onto the Latin one: same point size, its own
    # ascender, the Latin's baseline row.
    glyphs.update(bake(tf, [c for c in THAI if c in tcmap], top + la - ta, pad))
    drop, rise, shift = measure(thai, pt, pad)
    return glyphs, drop | (rise << 5) | (shift << 10), pt


def emit_face(h, c, name, box, glyphs, drop):
    cps = sorted(glyphs.keys())
    bits = bytearray()
    recs = []
    for cp in cps:
        adv, left, top, w, hh, packed = glyphs[cp]
        left = max(-128, min(127, left))
        top = max(-128, min(127, top))
        recs.append((len(bits), left, top, w, hh, min(adv, 255)))
        bits.extend(packed)
        while len(bits) % 4:
            bits.append(0)
    c.write('static const uint16_t %s_CPS[%d] PROGMEM = {\n' % (name, len(cps)))
    for i in range(0, len(cps), 16):
        c.write('    ' + ''.join('0x%04X,' % v for v in cps[i:i + 16]) + '\n')
    c.write('};\n')
    c.write('static const IntlGlyph %s_G[%d] PROGMEM = {\n' % (name, len(recs)))
    for off, l, t, w, hh, adv in recs:
        c.write('    {%d,%d,%d,%d,%d,%d},\n' % (off, l, t, w, hh, adv))
    c.write('};\n')
    n_blocks, zlen = intl_zip.emit_z(c, name, bits, [r[0] for r in recs])
    c.write(intl_zip.face_line(name, box, drop, len(cps), n_blocks) + '\n')
    h.write('extern const IntlFace %s;\n' % name)
    return zlen + len(recs) * 12 + len(cps) * 2


def main():
    h = open('src/fonts_script.h', 'w')
    c = open('src/fonts_script.cpp', 'w')
    for f in (h, c):
        f.write('// GENERATED by tools/make_fonts_script.py -- do not edit by hand.\n')
        f.write('// Each UI family\'s Latin past ASCII, Vietnamese and Thai, at the UI sizes:\n')
        f.write('// Noto Sans + Noto Sans Thai Looped (SCRIPT_NOTO_*), Prompt, Latin and Thai\n')
        f.write('// (SCRIPT_PLX_*). SIL OFL 1.1, see THIRD-PARTY.md.\n')
    h.write('#pragma once\n#include "fonts_intl.h"\n\n')
    c.write('#include "fonts_script.h"\n\n')
    total = 0
    for fam, latin, thai in FAMILIES:
        for box in SIZES:
            glyphs, drop, pt = build(latin, thai, box)
            n = emit_face(h, c, 'SCRIPT_%s_%d' % (fam, box), box, glyphs, drop)
            total += n
            print('%s %d px (%d pt): %d glyphs, %d KB, tone drop %d raise %d, tall shift %d'
                  % (fam, box, pt, len(glyphs), n // 1024, drop & 31, (drop >> 5) & 31,
                     drop >> 10), file=sys.stderr)
    print('total %d KB' % (total // 1024), file=sys.stderr)
    h.close()
    c.close()


if __name__ == '__main__':
    main()
