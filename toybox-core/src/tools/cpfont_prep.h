// Preparing a card font, once, so that opening an app is not a second of SD.
//
// The published packs are generous: Bitter at 16 pt is 962 KB -- four cuts of
// about 1,650 glyphs each, covering Latin, Greek, Cyrillic, Vietnamese,
// punctuation, arrows, box drawing and a few hundred emoji. Every one of those
// bytes crosses the bus when an app opens, and four line boxes means four
// files. That is the whole of the "notes opens slower now".
//
// So the first time a file is used it is read whole, cut down, and written back
// beside itself as a prepared copy. Every open after that reads the small one.
// Two things go:
//
//   - the bold-italic cut. gfx never draws it: bold beats italic there, and a
//     run asking for both gets the bold. A quarter of the file for a face the
//     panel cannot show.
//   - every codepoint a book is not set in. Latin, the combining marks, IPA
//     and Greek stay, and so does the punctuation block -- curly quotes, en
//     and em dashes, ellipsis, the things a real book is full of. Cyrillic,
//     Vietnamese, the symbol blocks and the emoji do not.
//
// Nothing vanishes off the panel: a codepoint the card face does not carry
// falls through to the baked tables, exactly as Thai already does under every
// Latin pack. It changes what a Russian page is SET in, not whether it draws.
//
// The prepared file is a valid .cpfont v4, which is the point -- cpfont.h reads
// it with no idea it was made here, and there is no second format to keep
// right. What marks it is four bytes in the header's unused space, and the
// length of the file it was made from: replace the font on the card and the
// prepared copy stops matching and is made again.
#pragma once
#include <stdint.h>
#include <string.h>

#include "cpfont.h"

namespace cpfont {

// What a prepared file keeps. Latin through Greek in one run -- U+0000 to
// U+03FF is ASCII, Latin-1, both Latin Extended blocks, IPA, the combining
// marks and Greek -- plus General Punctuation and the currency signs after it.
inline bool prepKeep(uint32_t cp) { return cp < 0x0400 || (cp >= 0x2000 && cp <= 0x20FF); }
inline bool prepCut(uint8_t cut) { return cut != BOLD_ITALIC; }

// The mark, in the header bytes nothing else uses (the format's own fields end
// at byte 13). A prepared file that does not match the font beside it any more
// is not read: the owner replaced the font, and the old copy is a ghost of it.
inline void prepStamp(uint8_t* h, uint32_t srcLen) {
  h[14] = 'T';
  h[15] = 'B';
  h[16] = 'X';
  h[17] = '1';
  memcpy(h + 18, &srcLen, 4);
}
inline bool prepStamped(const uint8_t* h, uint32_t n, uint32_t srcLen) {
  if (n < 32 || h[14] != 'T' || h[15] != 'B' || h[16] != 'X' || h[17] != '1') return false;
  uint32_t v;
  memcpy(&v, h + 18, 4);
  return v == srcLen;
}

namespace detail {

// Walk one style's intervals, handing every kept codepoint to `each` in the
// order its new glyph index will have. The plan pass and the write pass have
// to agree exactly -- a byte of disagreement is an offset table pointing into
// the middle of somebody else's glyph -- so they walk through here rather than
// each having their own loop.
//
// `each(cp, srcIndex, startsRun)`: startsRun is true when this codepoint does
// not follow the previous kept one, which is where a new interval begins.
template <typename F>
inline void prepWalk(const Font& f, const Style& s, F&& each) {
  uint32_t prevCp = 0;
  bool any = false;
  for (uint32_t k = 0; k < s.intervalCount; k++) {
    const uint8_t* iv = f.raw() + s.intervals + (size_t)k * 12;
    const uint32_t first = rd32(iv), last = rd32(iv + 4), fg = rd32(iv + 8);
    if (last < first) continue;
    for (uint32_t cp = first; cp <= last; cp++) {
      const uint32_t gi = fg + (cp - first);
      if (gi >= s.glyphCount) break;
      if (!prepKeep(cp)) continue;
      const bool startsRun = !any || cp != prevCp + 1;
      each(cp, gi, startsRun);
      prevCp = cp;
      any = true;
    }
  }
}

}  // namespace detail

// What one style costs when prepared.
struct PrepPlan {
  uint32_t intervals = 0, glyphs = 0, bitmaps = 0;
};

inline PrepPlan prepPlan(const Font& f, const Style& s) {
  PrepPlan p;
  detail::prepWalk(f, s, [&](uint32_t cp, uint32_t gi, bool startsRun) {
    (void)cp;
    if (startsRun) p.intervals++;
    p.glyphs++;
    Glyph g;
    if (f.glyphAt(s, gi, g)) p.bitmaps += g.bytes;
  });
  return p;
}

// How many bytes the prepared form of this font needs. 0 when there is nothing
// worth preparing -- a font already prepared, or one with no kept glyph in it.
inline uint32_t prepSize(const Font& f) {
  int kept = 0;
  uint32_t total = 32;
  uint32_t body = 0;
  for (int i = 0; i < f.styles(); i++) {
    const Style& s = f.style(i);
    if (!prepCut(s.cut)) continue;
    const PrepPlan p = prepPlan(f, s);
    if (p.glyphs == 0 || p.intervals == 0) continue;
    kept++;
    body += p.intervals * 12 + p.glyphs * 16 + p.bitmaps;
  }
  if (kept == 0) return 0;
  total += (uint32_t)kept * 32 + body;
  return total;
}

// Write the prepared file. Returns how many bytes it came to, or 0 -- and 0
// means "use the font as it is", never "the font is broken".
inline uint32_t prepare(const Font& f, uint32_t srcLen, uint8_t* dst, uint32_t cap) {
  const uint32_t need = prepSize(f);
  if (!need || need > cap) return 0;
  memset(dst, 0, 32);
  memcpy(dst, "CPFONT\0\0", 8);
  const uint16_t ver = VERSION;
  memcpy(dst + 8, &ver, 2);
  prepStamp(dst, srcLen);

  // The cuts that survive, and where each one's data will start. The table of
  // contents is written first and in full, so the offsets have to be known
  // before a byte of glyph data is laid down.
  int keptIdx[MAX_STYLES], kept = 0;
  PrepPlan plans[MAX_STYLES];
  for (int i = 0; i < f.styles() && kept < MAX_STYLES; i++) {
    const Style& s = f.style(i);
    if (!prepCut(s.cut)) continue;
    const PrepPlan p = prepPlan(f, s);
    if (p.glyphs == 0 || p.intervals == 0) continue;
    plans[kept] = p;
    keptIdx[kept] = i;
    kept++;
  }
  if (!kept) return 0;
  dst[12] = (uint8_t)kept;

  uint32_t at = 32 + (uint32_t)kept * 32;
  for (int k = 0; k < kept; k++) {
    const Style& s = f.style(keptIdx[k]);
    const PrepPlan& p = plans[k];
    uint8_t* t = dst + 32 + (size_t)k * 32;
    memset(t, 0, 32);
    t[0] = s.cut;
    memcpy(t + 4, &p.intervals, 4);
    memcpy(t + 8, &p.glyphs, 4);
    t[12] = s.advanceY;
    memcpy(t + 13, &s.ascender, 2);
    memcpy(t + 15, &s.descender, 2);
    // Bytes 17..23 stay zero: no kerning pairs, no classes, no ligatures. This
    // firmware draws a glyph at a time and read past all of it anyway.
    memcpy(t + 24, &at, 4);

    uint8_t* ivOut = dst + at;
    uint8_t* glOut = ivOut + (size_t)p.intervals * 12;
    uint8_t* bmOut = glOut + (size_t)p.glyphs * 16;
    uint32_t ivN = 0, glN = 0, bmN = 0;

    detail::prepWalk(f, s, [&](uint32_t cp, uint32_t gi, bool startsRun) {
      Glyph g;
      if (!f.glyphAt(s, gi, g)) {
        // A glyph the source cannot give is written as an empty one rather
        // than skipped: skipping would slide every codepoint after it one
        // place along inside its interval.
        g = Glyph();
      }
      if (startsRun) {
        uint8_t* iv = ivOut + (size_t)ivN * 12;
        const uint32_t one = cp, idx = glN;
        memcpy(iv, &one, 4);
        memcpy(iv + 4, &one, 4);
        memcpy(iv + 8, &idx, 4);
        ivN++;
      } else if (ivN) {
        // Still the same run: stretch the interval that is open.
        memcpy(ivOut + (size_t)(ivN - 1) * 12 + 4, &cp, 4);
      }
      uint8_t* go = glOut + (size_t)glN * 16;
      memset(go, 0, 16);
      go[0] = g.w;
      go[1] = g.h;
      memcpy(go + 2, &g.advance16, 2);
      memcpy(go + 4, &g.left, 2);
      memcpy(go + 6, &g.top, 2);
      memcpy(go + 8, &g.bytes, 2);
      memcpy(go + 12, &bmN, 4);
      if (g.bytes) {
        memcpy(bmOut + bmN, f.raw() + s.bitmaps + g.offset, g.bytes);
        bmN += g.bytes;
      }
      glN++;
    });
    at += p.intervals * 12 + p.glyphs * 16 + bmN;
  }
  return at;
}

}  // namespace cpfont
