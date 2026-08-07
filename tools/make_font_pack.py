#!/usr/bin/env python3
"""Builds downloadable font packs: the full character set of one language,
minus what the firmware already bakes in.

The baked tables (make_fonts_intl.py) carry the common sets -- GB2312 level 1,
KS X 1001, JIS level 1 -- so everyday text renders on a fresh device. A pack
extends one language to full coverage:

    zh_full   full CJK Unified Ideographs block that the SC face covers
    ko_full   all 11,172 composable Hangul syllables
    ja_full   full JIS X 0208 kanji

Each pack holds the same three sizes as the firmware (16/24/32) in one file the
device loads whole into PSRAM. Format, little-endian, every section 4-aligned:

    'TFP1'  u32 magic
    u32     face count
    per face:
      u32 box, u32 toneDrop, u32 count, u32 bitsLen
      u16 cps[count] (sorted), pad to 4
      IntlGlyph glyphs[count]   (the 12-byte record from fonts_intl.h)
      u8 bits[bitsLen], pad to 4

Run from the repo root:
    python3 tools/make_font_pack.py /tmp/packs
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_fonts_intl import (NOTO, SIZES, bake_group, build_face, cmap_of,
                             fit_ink, gb2312_level1, jis_level1, ksx1001, rng)
from PIL import ImageFont

BASIC = ImageFont.Layout.BASIC


def jis_all():
    out = []
    for cp in range(0x4E00, 0xA000):
        try:
            enc = chr(cp).encode('shift_jis')
        except UnicodeEncodeError:
            continue
        if len(enc) == 2:
            out.append(cp)
    return out


def uro(cmap):
    return [cp for cp in range(0x4E00, 0xA000) if cp in cmap]


def face_blob(box, cps, noto_index, samples):
    pt, dy = fit_ink(NOTO, noto_index, box, samples)
    font = ImageFont.truetype(NOTO, pt, index=noto_index, layout_engine=BASIC)
    cmap = cmap_of(NOTO, noto_index)
    glyphs = bake_group(font, cps, cmap, dy)
    return sorted(glyphs.keys()), glyphs


def pack_face(out, box, order, glyphs):
    bits = bytearray()
    grecs = bytearray()
    for cp in order:
        adv, left, top, w, h, packed = glyphs[cp]
        # 12 bytes, mirroring sizeof(IntlGlyph) with its tail padding
        grecs += struct.pack('<Ibb3B3x', len(bits), max(-128, min(127, left)),
                             max(-128, min(127, top)), w, h, min(adv, 255))
        bits += packed
        while len(bits) % 4:
            bits.append(0)
    out += struct.pack('<4I', box, 0, len(order), len(bits))
    for cp in order:
        out += struct.pack('<H', cp)
    while len(out) % 4:
        out.append(0)
    out += grecs
    out += bits
    while len(out) % 4:
        out.append(0)
    return len(order), len(bits)


def build_pack(path, cps, noto_index, samples, baked):
    todo = sorted(set(cps) - baked)
    out = bytearray(struct.pack('<4sI', b'TFP1', len(SIZES)))
    total = 0
    for box in SIZES:
        order, glyphs = face_blob(box, todo, noto_index, samples)
        n, b = pack_face(out, box, order, glyphs)
        total = n
    open(path, 'wb').write(out)
    print('%s: %d glyphs/size, %d KB' % (path, total, len(out) // 1024),
          file=sys.stderr)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else '/tmp/packs'
    os.makedirs(outdir, exist_ok=True)
    baked_han = set(gb2312_level1()) | set(jis_level1())
    baked_hangul = set(ksx1001())
    sc_cmap = cmap_of(NOTO, 2)

    build_pack(os.path.join(outdir, 'zh_full.tfp'), uro(sc_cmap), 2, '鬱国。番',
               baked_han)
    build_pack(os.path.join(outdir, 'ko_full.tfp'), rng(0xAC00, 0xD7A3), 1,
               '뷁흑。', baked_hangul)
    build_pack(os.path.join(outdir, 'ja_full.tfp'), jis_all(), 0, 'ぽ鬱。',
               baked_han)


if __name__ == '__main__':
    main()
