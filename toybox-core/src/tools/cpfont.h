// Reading CrossInk's .cpfont files.
//
// CrossInk and CrossPoint keep their card fonts as .cpfont: one file per
// family and size, holding up to four styles, under /.fonts/<Family>/ or
// /fonts/<Family>/ on the card. There is a web builder and a Python script
// that make them, and font packs already published for people to download.
// Reading their format rather than inventing a third one means a card carrying
// fonts works in either firmware, which is the same promise the reading
// positions already keep -- and it means nobody has to build a converter.
//
// This parses a whole file already in memory. It does not own it: the caller
// loads the bytes (from the card, into PSRAM) and keeps them alive for as long
// as any Style or Glyph from them is in use.
//
// The layout, from CrossInk's own writer and loader:
//
//   header  32 bytes   "CPFONT\0\0", u16 version, u16 flags, u8 styleCount
//   TOC     32 bytes per style, in the file's style order
//   then, per style, at the TOC's dataOffset:
//     intervals   12 bytes each   u32 first, u32 last, u32 firstGlyphIndex
//     glyphs      16 bytes each   see Glyph below
//     kern left    3 bytes each   ignored here
//     kern right   3 bytes each   ignored here
//     kern matrix  leftClasses * rightClasses bytes, ignored here
//     ligatures    8 bytes each   ignored here
//     bitmaps      2 bits per pixel, no padding between rows OR glyphs
//
// Kerning and ligatures are read past rather than read: this device draws a
// glyph at a time with no pair adjustment, and pretending otherwise would cost
// memory to no visible end. The sizes still have to be right, because the
// bitmaps sit after them.
#pragma once
#include <stdint.h>
#include <string.h>

namespace cpfont {

inline constexpr uint16_t VERSION = 4;  // the only one this reads
inline constexpr int MAX_STYLES = 4;

// The four cuts a file can carry, in CrossInk's numbering.
enum Cut : uint8_t { REGULAR = 0, BOLD = 1, ITALIC = 2, BOLD_ITALIC = 3 };

struct Glyph {
  uint8_t w = 0, h = 0;      // the bitmap, in pixels
  uint16_t advance16 = 0;    // pen movement in sixteenths of a pixel
  int16_t left = 0, top = 0; // bearings from the pen, y up
  uint16_t bytes = 0;        // of bitmap
  uint32_t offset = 0;       // into the style's bitmap section
  int advance() const { return (advance16 + 8) >> 4; }  // rounded, in pixels
};

struct Style {
  uint8_t cut = REGULAR;
  uint32_t intervalCount = 0, glyphCount = 0;
  uint8_t advanceY = 0;       // the line box in pixels: how tall this size is
  int16_t ascender = 0, descender = 0;
  // Absolute offsets into the blob, so nothing has to be recomputed per glyph.
  uint32_t intervals = 0, glyphs = 0, bitmaps = 0;
};

namespace detail {
inline uint16_t rd16(const uint8_t* p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}
inline uint32_t rd32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
inline int16_t rds16(const uint8_t* p) { return (int16_t)rd16(p); }
}  // namespace detail

class Font {
 public:
  // False for anything this cannot read -- a truncated file, another version,
  // a section that runs past the end. A font that fails here is skipped, not
  // half-used: half a font is a page of blanks nobody can explain.
  bool open(const uint8_t* data, uint32_t len) {
    using namespace detail;
    _data = nullptr;
    _n = 0;
    if (!data || len < 32) return false;
    if (memcmp(data, "CPFONT\0\0", 8) != 0) return false;
    if (rd16(data + 8) != VERSION) return false;
    const int n = data[12];
    if (n < 1 || n > MAX_STYLES) return false;
    if (len < (uint32_t)(32 + n * 32)) return false;

    for (int i = 0; i < n; i++) {
      const uint8_t* t = data + 32 + i * 32;
      Style s;
      s.cut = t[0];
      s.intervalCount = rd32(t + 4);
      s.glyphCount = rd32(t + 8);
      s.advanceY = t[12];
      s.ascender = rds16(t + 13);
      s.descender = rds16(t + 15);
      const uint16_t kernL = rd16(t + 17), kernR = rd16(t + 19);
      const uint8_t kernLCls = t[21], kernRCls = t[22], ligs = t[23];
      const uint32_t base = rd32(t + 24);

      // Every section's size, in the order they are written. 64-bit
      // arithmetic throughout: a corrupt count would otherwise wrap and
      // produce an offset that looks perfectly reasonable.
      const uint64_t intervals = base;
      const uint64_t glyphs = intervals + (uint64_t)s.intervalCount * 12;
      const uint64_t kernLeft = glyphs + (uint64_t)s.glyphCount * 16;
      const uint64_t kernRight = kernLeft + (uint64_t)kernL * 3;
      const uint64_t kernMatrix = kernRight + (uint64_t)kernR * 3;
      const uint64_t ligatures = kernMatrix + (uint64_t)kernLCls * kernRCls;
      const uint64_t bitmaps = ligatures + (uint64_t)ligs * 8;
      if (bitmaps > len) return false;  // the bitmaps have to start inside the file
      if (s.glyphCount == 0 || s.intervalCount == 0) return false;

      s.intervals = (uint32_t)intervals;
      s.glyphs = (uint32_t)glyphs;
      s.bitmaps = (uint32_t)bitmaps;
      _s[i] = s;
    }
    _data = data;
    _len = len;
    _n = n;
    return true;
  }

  int styles() const { return _n; }
  const Style& style(int i) const { return _s[i]; }

  // The index of a cut, or -1. A file with only a regular cut answers -1 for
  // bold, and the caller draws the regular one twice offset by a pixel, which
  // is what the baked faces already do for a missing bold.
  int find(uint8_t cut) const {
    for (int i = 0; i < _n; i++)
      if (_s[i].cut == cut) return i;
    return -1;
  }

  // The glyph for a codepoint, or false when this style does not carry it --
  // which is normal and means "ask the baked tables".
  bool glyph(const Style& s, uint32_t cp, Glyph& out) const {
    using namespace detail;
    if (!_data) return false;
    // The intervals are sorted, so this is a binary search rather than a walk
    // past three thousand of them for a character near the end.
    uint32_t lo = 0, hi = s.intervalCount;
    while (lo < hi) {
      const uint32_t mid = (lo + hi) / 2;
      const uint8_t* iv = _data + s.intervals + (size_t)mid * 12;
      const uint32_t first = rd32(iv), last = rd32(iv + 4);
      if (cp < first) {
        hi = mid;
      } else if (cp > last) {
        lo = mid + 1;
      } else {
        return glyphAt(s, rd32(iv + 8) + (cp - first), out);
      }
    }
    return false;
  }

  // A glyph by its place in the style's table rather than by codepoint, for
  // anything walking the table itself -- preparing a cut-down copy of the file
  // is the one caller (see cpfont_prep.h).
  bool glyphAt(const Style& s, uint32_t idx, Glyph& out) const {
    using namespace detail;
    if (!_data || idx >= s.glyphCount) return false;
    const uint8_t* g = _data + s.glyphs + (size_t)idx * 16;
    out.w = g[0];
    out.h = g[1];
    out.advance16 = rd16(g + 2);
    out.left = rds16(g + 4);
    out.top = rds16(g + 6);
    out.bytes = rd16(g + 8);
    out.offset = rd32(g + 12);
    // A glyph whose bitmap runs off the end is a glyph this does not have.
    if ((uint64_t)s.bitmaps + out.offset + out.bytes > _len) return false;
    return true;
  }

  // The bytes themselves, for the same one caller.
  const uint8_t* raw() const { return _data; }
  uint32_t length() const { return _len; }

  // Ink at (x, y) of a glyph, 0 (paper) to 3 (black). The bitmap is a
  // continuous 2-bit stream: no padding at the end of a row, and none between
  // one glyph and the next, so a row-based reader lands a pixel or two out on
  // every glyph whose width is not a multiple of four -- which looks like a
  // font that is subtly, unfixably wrong.
  uint8_t ink(const Style& s, const Glyph& g, int x, int y) const {
    if (!_data || x < 0 || y < 0 || x >= g.w || y >= g.h) return 0;
    const uint32_t idx = (uint32_t)y * g.w + (uint32_t)x;
    const uint32_t byte = idx >> 2;
    if (byte >= g.bytes) return 0;
    const uint8_t b = _data[s.bitmaps + g.offset + byte];
    const int shift = (3 - (int)(idx & 3)) * 2;
    return (uint8_t)((b >> shift) & 3);
  }

  // The threshold that turns their anti-aliased grey into this panel's black
  // and white. 2 of 3 keeps stems solid and drops the faintest edge pixels;
  // 1 of 3 fattens every letter by a pixel all round, which at 16 px reads as
  // a different, worse font.
  bool on(const Style& s, const Glyph& g, int x, int y) const { return ink(s, g, x, y) >= 2; }

 private:
  const uint8_t* _data = nullptr;
  uint32_t _len = 0;
  int _n = 0;
  Style _s[MAX_STYLES];
};

}  // namespace cpfont
