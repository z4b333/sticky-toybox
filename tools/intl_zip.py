#!/usr/bin/env python3
"""Block compression for the baked international glyph bitmaps.

The bitmaps of fonts_intl.cpp are most of the app image (1.7 MB of 3.9 MB in
v1.4.4). They compress well -- a 1-bit glyph is mostly runs of white -- so
they are stored as raw-deflate blocks of about BLOCK_TARGET bytes, cut only at
a glyph boundary so a glyph never straddles two blocks. gfx.cpp inflates a
block into a PSRAM slot the first time one of its glyphs is drawn and keeps
the most recently used slots.

Used two ways:
  * by make_fonts_intl.py, which calls emit_z() while generating, and
  * on its own, to compress an existing raw fonts_intl.cpp in place without
    re-rasterising (the glyphs stay byte-identical to the shipped ones):
        python3 tools/intl_zip.py src/fonts_intl.cpp src/fonts_intl.h
"""
import re
import sys
import zlib

BLOCK_TARGET = 4096
# A slot must hold the largest block: the target plus the glyph that crossed
# it. The biggest baked glyph (32 px CJK) is ~140 bytes; 512 is plenty, and
# the generator refuses to write a block that would not fit.
BLOCK_MAX = BLOCK_TARGET + 512


def blocks(bits, offs):
    """Cut points (raw offsets) at glyph starts, ~BLOCK_TARGET apart."""
    starts = sorted(set(offs))
    cuts = [0]
    for s in starts:
        if s - cuts[-1] >= BLOCK_TARGET:
            cuts.append(s)
    cuts.append(len(bits))
    for a, b in zip(cuts, cuts[1:]):
        if b - a > BLOCK_MAX:
            raise SystemExit('block %d..%d is %d bytes > BLOCK_MAX' % (a, b, b - a))
    return cuts


def deflate_raw(data):
    co = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    return co.compress(data) + co.flush()


def emit_z(c, name, bits, offs):
    """Writes NAME_ZRAW / NAME_ZOFF / NAME_Z; returns (n_blocks, z_len)."""
    cuts = blocks(bits, offs)
    z = bytearray()
    zoff = [0]
    for a, b in zip(cuts, cuts[1:]):
        z += deflate_raw(bytes(bits[a:b]))
        zoff.append(len(z))
    n = len(cuts) - 1
    c.write('static const uint32_t %s_ZRAW[%d] PROGMEM = {\n' % (name, n + 1))
    for i in range(0, n + 1, 8):
        c.write('    ' + ''.join('%d,' % v for v in cuts[i:i + 8]) + '\n')
    c.write('};\n')
    c.write('static const uint32_t %s_ZOFF[%d] PROGMEM = {\n' % (name, n + 1))
    for i in range(0, n + 1, 8):
        c.write('    ' + ''.join('%d,' % v for v in zoff[i:i + 8]) + '\n')
    c.write('};\n')
    c.write('static const uint8_t %s_Z[%d] PROGMEM = {\n' % (name, len(z)))
    for i in range(0, len(z), 16):
        c.write('    ' + ''.join('0x%02X,' % v for v in z[i:i + 16]) + '\n')
    c.write('};\n')
    return n, len(z)


def face_line(name, box, drop, count, n_blocks):
    return ('const IntlFace %s = {%d, %d, %d, %s_CPS, %s_G, nullptr, %d, %s_ZRAW, %s_ZOFF, %s_Z};\n'
            % (name, box, drop, count, name, name, n_blocks, name, name, name))


HEADER_STRUCT = '''struct IntlFace {
  uint16_t box;       // line box the glyphs were baked into
  uint16_t toneDrop;  // px a lone Thai tone mark descends (measured)
  uint32_t count;
  const uint16_t* cps;      // sorted; binary-search me
  const IntlGlyph* glyphs;  // parallel to cps
  // Raw bitmaps (font packs). nullptr for the baked faces, whose bitmaps are
  // raw-deflate blocks instead (tools/intl_zip.py): block i holds raw bytes
  // zRaw[i]..zRaw[i+1] and its stream is z[zOff[i]..zOff[i+1]].
  const uint8_t* bits;
  uint32_t zBlocks;
  const uint32_t* zRaw;
  const uint32_t* zOff;
  const uint8_t* z;
};

// Largest raw block of any baked face: the size of one inflate slot.
#define INTL_ZBLOCK_MAX %d
''' % BLOCK_MAX


def main():
    cpp, hdr = sys.argv[1], sys.argv[2]
    src = open(cpp).read()
    out = []
    pos = 0
    face_re = re.compile(
        r'const IntlFace (INTL_\d+) = \{(\d+), (\d+), (\d+), \1_CPS, \1_G, \1_BITS\};\n')
    for m in face_re.finditer(src):
        name, box, drop, count = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        g = re.search(r'static const IntlGlyph %s_G\[\d+\] PROGMEM = \{\n(.*?)\};\n' % name,
                      src[pos:], re.S)
        offs = [int(x) for x in re.findall(r'\{(\d+),', g.group(1))]
        b = re.search(r'static const uint8_t %s_BITS\[(\d+)\] PROGMEM = \{\n(.*?)\};\n' % name,
                      src[pos:], re.S)
        bits = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', b.group(2)))
        assert len(bits) == int(b.group(1)), name
        out.append(src[pos:pos + b.start()])

        class W:
            def __init__(self):
                self.s = []

            def write(self, t):
                self.s.append(t)
        w = W()
        n, zl = emit_z(w, name, bits, offs)
        out.append(''.join(w.s))
        out.append(src[pos + b.end():m.start()])
        out.append(face_line(name, box, drop, count, n))
        pos = m.end()
        print('%s: %d KB raw -> %d KB in %d blocks' % (name, len(bits) // 1024, zl // 1024, n),
              file=sys.stderr)
    out.append(src[pos:])
    open(cpp, 'w').write(''.join(out))

    h = open(hdr).read()
    h2 = re.sub(r'struct IntlFace \{.*?\n\};\n', lambda _m: HEADER_STRUCT, h, flags=re.S)
    assert h2 != h, 'IntlFace struct not found in header'
    open(hdr, 'w').write(h2)


if __name__ == '__main__':
    main()
