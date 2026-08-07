#!/usr/bin/env python3
"""Bakes the international faces: Thai, Chinese, Korean, Japanese kana,
Vietnamese and accented European Latin, at the three content sizes.

Coverage is chosen by encoding standard rather than by hand:
  * Chinese  -- GB2312 level 1, the 3,755 most common simplified characters
                (derived by round-tripping the codec, not from a shipped list).
  * Korean   -- KS X 1001, the 2,350 syllables of practical Korean, plus the
                compatibility jamo.
  * Japanese -- all kana, plus JIS X 0208 level 1 kanji (union with the Chinese
                set; drawn from the SC face, one style for all han).
  * Thai     -- the full block, from Loma (proven by tools/thai_proof.py).
  * Latin    -- Latin-1 Supplement, Extended-A and the Vietnamese Extended
                Additional block, from DejaVu so it matches the UI faces.

Unlike the ASCII tables (fonts_ui.h), these glyphs carry a signed bearing and a
real advance, because combining marks draw backwards over the glyph before them.
ASCII itself stays in the old tables -- the two systems meet in gfx.cpp.

Run from the repo root:
    python3 tools/make_fonts_intl.py
writes src/fonts_intl.h and src/fonts_intl.cpp, and prints a size budget.
"""
import sys
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont, TTCollection

DEJAVU = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
LOMA = '/usr/share/fonts/opentype/tlwg/Loma.otf'
NOTO = '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc'
THR = 140
BASIC = ImageFont.Layout.BASIC
SIZES = [16, 24, 32]  # the content boxes; 12 px CJK/Thai is not legible ink


# --- character sets ----------------------------------------------------------

def gb2312_level1():
    out = []
    for cp in range(0x4E00, 0xA000):
        try:
            enc = chr(cp).encode('gb2312')
        except UnicodeEncodeError:
            continue
        if len(enc) == 2 and 0xB0 <= enc[0] <= 0xD7:
            out.append(cp)
    return out


def jis_level1():
    out = []
    for cp in range(0x4E00, 0xA000):
        try:
            enc = chr(cp).encode('shift_jis')
        except UnicodeEncodeError:
            continue
        if len(enc) == 2 and 0x889F <= (enc[0] << 8 | enc[1]) <= 0x9872:
            out.append(cp)
    return out


def ksx1001():
    # Python's euc_kr codec is really UHC and encodes all 11,172 syllables; the
    # 2,350 of KS X 1001 proper are the ones whose lead byte lands in the
    # standard's syllable rows (0xB0-0xC8).
    out = []
    for cp in range(0xAC00, 0xD7A4):
        try:
            enc = chr(cp).encode('euc_kr')
        except UnicodeEncodeError:
            continue
        if len(enc) == 2 and 0xB0 <= enc[0] <= 0xC8 and 0xA1 <= enc[1] <= 0xFE:
            out.append(cp)
    return out


def rng(a, b):
    return list(range(a, b + 1))


LATIN = rng(0xA1, 0xFF) + rng(0x100, 0x17F) + rng(0x1E00, 0x1EFF) + [
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x20AC, 0x20A9, 0x20AB]
THAI = rng(0x0E01, 0x0E3A) + rng(0x0E3F, 0x0E5B)
KANA = rng(0x3041, 0x3096) + rng(0x3099, 0x309F) + rng(0x30A0, 0x30FF)
JAMO = rng(0x3131, 0x3163)
CJK_PUNCT = rng(0x3001, 0x3017) + [0x301C] + rng(0xFF01, 0xFF5E) + [0xFFE5]


# --- rasterisation -----------------------------------------------------------

def cmap_of(path, index):
    if path.endswith('.ttc'):
        f = TTCollection(path, lazy=True).fonts[index]
    else:
        f = TTFont(path, lazy=True)
    return set(f.getBestCmap().keys())


def fit_metrics(path, index, box):
    """Largest point size whose full metrics fit the box (Latin, Thai)."""
    best = 4
    for pt in range(4, box * 3):
        f = ImageFont.truetype(path, pt, index=index or 0)
        asc, desc = f.getmetrics()
        if asc + desc <= box:
            best = pt
    return best, 0


def fit_ink(path, index, box, samples):
    """Largest point size whose dense sample glyphs fit the box, plus the shift
    that centres their band. CJK metrics claim ~1.4x em, but the glyphs are
    square; fitting on metrics would waste a third of the box."""
    for pt in range(box + box // 2, 3, -1):
        f = ImageFont.truetype(path, pt, index=index, layout_engine=BASIC)
        top, bot = box, 0
        for ch in samples:
            m, (dx, dy) = f.getmask2(ch)
            if m.size[1] == 0:
                continue
            top = min(top, dy)
            bot = max(bot, dy + m.size[1])
        if bot - top <= box:
            return pt, (box - (bot - top)) // 2 - top
    return 4, 0


def tone_drop(box, pt):
    """Measured, not guessed: how far a lone tone mark must descend. See
    tools/thai_proof.py for the derivation."""
    shaped = ImageFont.truetype(LOMA, pt)
    basic = ImageFont.truetype(LOMA, pt, layout_engine=BASIC)
    pad = box * 3

    def top(font, text):
        im = Image.new('L', (pad * 3, pad * 3), 255)
        ImageDraw.Draw(im).text((pad, pad), text, font=font, fill=0)
        q = im.load()
        ys = [y for y in range(pad * 3) for x in range(pad * 3) if q[x, y] < THR]
        return min(ys) - pad if ys else 0

    deltas = sorted(top(shaped, c) - top(basic, c)
                    for c in ('ก่', 'ก้', 'ห่', 'ป่'))
    return max(deltas[len(deltas) // 2], 0)


def bake_group(font, cps, cmap, dy):
    """codepoint -> (adv, left, top, w, h, packed rows)"""
    out = {}
    for cp in cps:
        if cp not in cmap:
            continue
        ch = chr(cp)
        mask, (dx, dyg) = font.getmask2(ch)
        adv = max(int(round(font.getlength(ch))), 0)
        w, h = mask.size
        if w == 0 or h == 0:
            out[cp] = (adv, 0, 0, 0, 0, b'')
            continue
        data = Image.frombytes('L', (w, h), bytes(mask)).tobytes()
        stride = (w + 7) // 8
        packed = bytearray(stride * h)
        for y in range(h):
            for x in range(w):
                if data[y * w + x] >= THR:
                    packed[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
        out[cp] = (adv, dx, dyg + dy, w, h, bytes(packed))
    return out


def build_face(box):
    han = sorted(set(gb2312_level1()) | set(jis_level1()))
    hangul = ksx1001()

    dj_pt, _ = fit_metrics(DEJAVU, None, box)
    lo_pt, _ = fit_metrics(LOMA, None, box)
    sc_pt, sc_dy = fit_ink(NOTO, 2, box, '鬱国。番')
    kr_pt, kr_dy = fit_ink(NOTO, 1, box, '뷁흑。')
    jp_pt, jp_dy = fit_ink(NOTO, 0, box, 'ぽグ。')

    groups = [
        (DEJAVU, None, dj_pt, 0, LATIN),
        (LOMA, None, lo_pt, 0, THAI),
        (NOTO, 2, sc_pt, sc_dy, han + CJK_PUNCT),
        (NOTO, 1, kr_pt, kr_dy, hangul + JAMO),
        (NOTO, 0, jp_pt, jp_dy, KANA),
    ]
    glyphs = {}
    for path, index, pt, dy, cps in groups:
        font = ImageFont.truetype(path, pt, index=index or 0, layout_engine=BASIC)
        cmap = cmap_of(path, index or 0)
        for cp, g in bake_group(font, cps, cmap, dy).items():
            glyphs.setdefault(cp, g)  # first group wins on overlap
    return glyphs, tone_drop(box, lo_pt)


# --- emission ----------------------------------------------------------------

def emit_face(h, c, box, glyphs, drop):
    name = 'INTL_%d' % box
    cps = sorted(glyphs.keys())
    bits = bytearray()
    recs = []
    for cp in cps:
        adv, left, top, w, hh, packed = glyphs[cp]
        # clamp bearings into the record's int8 range; nothing real gets near it
        left = max(-128, min(127, left))
        top = max(-128, min(127, top))
        recs.append((len(bits), left, top, w, hh, min(adv, 255)))
        bits.extend(packed)
        while len(bits) % 4:
            bits.append(0)  # keep every off 4-aligned; Xtensa dislikes worse

    c.write('static const uint16_t %s_CPS[%d] PROGMEM = {\n' % (name, len(cps)))
    for i in range(0, len(cps), 16):
        c.write('    ' + ''.join('0x%04X,' % v for v in cps[i:i + 16]) + '\n')
    c.write('};\n')
    c.write('static const IntlGlyph %s_G[%d] PROGMEM = {\n' % (name, len(recs)))
    for off, l, t, w, hh, adv in recs:
        c.write('    {%d,%d,%d,%d,%d,%d},\n' % (off, l, t, w, hh, adv))
    c.write('};\n')
    c.write('static const uint8_t %s_BITS[%d] PROGMEM = {\n' % (name, len(bits)))
    for i in range(0, len(bits), 16):
        c.write('    ' + ''.join('0x%02X,' % b for b in bits[i:i + 16]) + '\n')
    c.write('};\n')
    c.write('const IntlFace %s = {%d, %d, %d, %s_CPS, %s_G, %s_BITS};\n\n'
            % (name, box, drop, len(cps), name, name, name))
    h.write('extern const IntlFace %s;\n' % name)
    return len(bits) + len(recs) * 12 + len(cps) * 2


def main():
    h = open('src/fonts_intl.h', 'w')
    c = open('src/fonts_intl.cpp', 'w')
    for f in (h, c):
        f.write('// GENERATED by tools/make_fonts_intl.py -- do not edit by hand.\n')
        f.write('// Thai (Loma), simplified Chinese GB2312-L1 + JIS-L1 kanji, Korean KS X 1001,\n')
        f.write('// kana, Vietnamese and European Latin (DejaVu), with signed bearings and real\n')
        f.write('// advances. ASCII stays in fonts_ui.h; the two meet in gfx.cpp.\n')
    h.write('#pragma once\n#include <pgmspace.h>\n#include <stdint.h>\n\n')
    h.write('// A glyph that knows where it sits: `left` may be negative (combining marks\n')
    h.write('// reach back over the previous glyph) and `top` is from the top of the line box.\n')
    h.write('// Deliberately NOT packed: a 12-byte aligned record keeps the uint32 offset\n')
    h.write('// on a word boundary, which flash-mapped reads on the S3 want.\n')
    h.write('struct IntlGlyph {\n')
    h.write('  uint32_t off;\n  int8_t left;\n  int8_t top;\n')
    h.write('  uint8_t w;\n  uint8_t h;\n  uint8_t adv;\n};\n\n')
    h.write('struct IntlFace {\n')
    h.write('  uint16_t box;       // line box the glyphs were baked into\n')
    h.write('  uint16_t toneDrop;  // px a lone Thai tone mark descends (measured)\n')
    h.write('  uint32_t count;\n')
    h.write('  const uint16_t* cps;      // sorted; binary-search me\n')
    h.write('  const IntlGlyph* glyphs;  // parallel to cps\n')
    h.write('  const uint8_t* bits;\n};\n\n')
    c.write('#include "fonts_intl.h"\n\n')

    total = 0
    for box in SIZES:
        glyphs, drop = build_face(box)
        n = emit_face(h, c, box, glyphs, drop)
        total += n
        print('box %d: %d glyphs, %d KB, toneDrop %d' % (box, len(glyphs), n // 1024, drop),
              file=sys.stderr)
    print('total %d KB' % (total // 1024), file=sys.stderr)
    h.close()
    c.close()


if __name__ == '__main__':
    main()
