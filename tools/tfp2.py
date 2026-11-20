#!/usr/bin/env python3
"""Compressed font packs (TFP2) and the base pack.

A TFP1 pack keeps its glyph bitmaps raw, so the device can map it straight out
of flash. Raw, the three language packs were 7.45 MB of the 16 MB chip; as
raw-deflate blocks they are 3.3 MB. TFP2 stores the same faces in the same
blocks the baked tables always used (tools/intl_zip.py), and gfx.cpp inflates
a block into a PSRAM slot the first time one of its glyphs is drawn -- the
path the baked faces have taken since v1.4.

The BASE pack is what used to be baked into the app: GB2312 level 1, JIS
level 1 kanji, KS X 1001 Hangul and kana (tools/fonts/fonts_intl.cpp, written
by make_fonts_intl.py). It moved out to make room for two OTA app slots. It is
read from that generated file, so its glyphs stay byte-identical to the ones
devices have shown since v1.4 -- no re-rasterising, no CJK fonts needed.

Format, little-endian, every section 4-aligned:

    'TFP2'  u32 magic
    u32     face count
    u32     flags         bit 0: base pack (fills the baked slots, not a pack slot)
    per face:
      u32 box, u32 toneDrop, u32 count, u32 zBlocks, u32 zLen
      u16 cps[count] (sorted), pad to 4
      IntlGlyph glyphs[count]   (the 12-byte record from fonts_intl.h)
      u32 zRaw[zBlocks + 1]     raw offset where each block starts, then the end
      u32 zOff[zBlocks + 1]     the same, in z
      u8  z[zLen], pad to 4

Usage, from the repo root:
    python3 tools/tfp2.py base tools/fonts/fonts_intl.cpp docs/firmware/base_font.tfp
    python3 tools/tfp2.py zip  in.tfp out.tfp      # TFP1 -> TFP2, glyphs unchanged
    python3 tools/tfp2.py check file.tfp           # inflates every block, prints sizes
"""
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from intl_zip import BLOCK_MAX, blocks, deflate_raw  # noqa: E402

FLAG_BASE = 1
GLYPH = struct.Struct('<Ibb3B3x')  # mirrors IntlGlyph, tail padding included


def pad4(b):
    while len(b) % 4:
        b.append(0)


# A face is (box, toneDrop, cps[list], glyphs[list of (off,left,top,w,h,adv)],
#            raw bits OR (zraw, zoff, z)).

def write(path, faces, flags=0):
    out = bytearray(struct.pack('<4sII', b'TFP2', len(faces), flags))
    for box, drop, cps, glyphs, data in faces:
        if isinstance(data, (bytes, bytearray)):
            cuts = blocks(data, [g[0] for g in glyphs])
            z = bytearray()
            zoff = [0]
            for a, b in zip(cuts, cuts[1:]):
                z += deflate_raw(bytes(data[a:b]))
                zoff.append(len(z))
            zraw = cuts
        else:
            zraw, zoff, z = data
        n = len(zraw) - 1
        out += struct.pack('<5I', box, drop, len(cps), n, len(z))
        for cp in cps:
            out += struct.pack('<H', cp)
        pad4(out)
        for g in glyphs:
            out += GLYPH.pack(*g)
        out += struct.pack('<%dI' % (n + 1), *zraw)
        out += struct.pack('<%dI' % (n + 1), *zoff)
        out += z
        pad4(out)
    with open(path, 'wb') as f:
        f.write(out)
    return len(out)


def read(path):
    """Returns (magic, flags, faces) with every face's raw bits recovered."""
    d = open(path, 'rb').read()
    magic = d[:4]
    nf = struct.unpack_from('<I', d, 4)[0]
    at = 8
    flags = 0
    if magic == b'TFP2':
        flags = struct.unpack_from('<I', d, 8)[0]
        at = 12
    elif magic != b'TFP1':
        raise SystemExit('%s: not a font pack' % path)
    faces = []
    for _ in range(nf):
        if magic == b'TFP1':
            box, drop, count, bits_len = struct.unpack_from('<4I', d, at)
            at += 16
        else:
            box, drop, count, nblk, zlen = struct.unpack_from('<5I', d, at)
            at += 20
        cps = list(struct.unpack_from('<%dH' % count, d, at))
        at += count * 2
        at = (at + 3) & ~3
        glyphs = [GLYPH.unpack_from(d, at + i * 12) for i in range(count)]
        at += count * 12
        if magic == b'TFP1':
            bits = d[at:at + bits_len]
            at += bits_len
        else:
            zraw = struct.unpack_from('<%dI' % (nblk + 1), d, at)
            at += (nblk + 1) * 4
            zoff = struct.unpack_from('<%dI' % (nblk + 1), d, at)
            at += (nblk + 1) * 4
            z = d[at:at + zlen]
            at += zlen
            bits = bytearray()
            for i in range(nblk):
                raw = zlib.decompress(z[zoff[i]:zoff[i + 1]], -15)
                if len(raw) != zraw[i + 1] - zraw[i] or len(raw) > BLOCK_MAX:
                    raise SystemExit('%s: face %d block %d is bad' % (path, box, i))
                bits += raw
        at = (at + 3) & ~3
        if at > len(d):
            raise SystemExit('%s: truncated' % path)
        faces.append((box, drop, cps, glyphs, bytes(bits)))
    return magic, flags, faces


# --- the generated C file -------------------------------------------------------

def _array(src, name):
    m = re.search(r'%s\[\d+\] PROGMEM = \{(.*?)\};' % re.escape(name), src, re.S)
    if not m:
        raise SystemExit('fonts_intl.cpp: no %s' % name)
    return m.group(1)


def _ints(body):
    return [int(v, 0) for v in re.findall(r'-?0x[0-9A-Fa-f]+|-?\d+', body)]


def from_intl_cpp(path):
    src = open(path, encoding='utf-8').read()
    faces = []
    for m in re.finditer(r'const IntlFace (INTL_\d+) = \{(\d+), (\d+), (\d+),', src):
        name, box, drop, count = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        cps = _ints(_array(src, name + '_CPS'))
        g = _ints(_array(src, name + '_G'))
        glyphs = [tuple(g[i:i + 6]) for i in range(0, len(g), 6)]
        zraw = _ints(_array(src, name + '_ZRAW'))
        zoff = _ints(_array(src, name + '_ZOFF'))
        z = bytes(_ints(_array(src, name + '_Z')))
        if len(cps) != count or len(glyphs) != count:
            raise SystemExit('%s: %d cps, %d glyphs, header says %d' % (name, len(cps), len(glyphs), count))
        faces.append((box, drop, cps, glyphs, (zraw, zoff, z)))
    if not faces:
        raise SystemExit('%s: no faces' % path)
    return faces


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'base':
        n = write(sys.argv[3], from_intl_cpp(sys.argv[2]), FLAG_BASE)
        print('%s: base pack, %d KB' % (sys.argv[3], n // 1024))
    elif cmd == 'zip':
        magic, flags, faces = read(sys.argv[2])
        n = write(sys.argv[3], faces, flags)
        print('%s: %d KB -> %s: %d KB' % (sys.argv[2], os.path.getsize(sys.argv[2]) // 1024,
                                          sys.argv[3], n // 1024))
    elif cmd == 'check':
        magic, flags, faces = read(sys.argv[2])
        for box, drop, cps, glyphs, bits in faces:
            end = max(g[0] + ((g[3] + 7) // 8) * g[4] for g in glyphs) if glyphs else 0
            if end > len(bits):
                raise SystemExit('box %d: glyph runs past the bitmap' % box)
            print('%s box %d: %d glyphs, %d KB raw' % (magic.decode(), box, len(cps), len(bits) // 1024))
        print('flags %d, %d KB on disk, ok' % (flags, os.path.getsize(sys.argv[2]) // 1024))
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
