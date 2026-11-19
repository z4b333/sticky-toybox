// thai_shape.h -- Thai marks placed with a card font's own shifted forms.
//
// A .cpfont has no shaping tables: a Thai mark is a zero-advance glyph hung at
// one fixed height and one fixed offset. That reads, but it is the "average"
// placement -- a tone mark always sits at second-storey height even over a bare
// consonant, and an upper vowel on a tall consonant (ป ฝ ฟ ฬ) runs into its
// tail.
//
// The Thai faces people put on the card (TLWG's Loma, Garuda, Norasi, ...)
// carry the fix as glyphs in the Private Use Area at U+F700-F71A, the layout
// Windows and macOS shaping use: marks moved left for the tall consonants,
// tone marks brought down where no upper vowel sits, ญ and ฐ without the tail
// a lower vowel would collide with, lower vowels dropped under ฎ and ฏ. This
// picks the form; the caller draws it if the face has it and the plain mark
// otherwise, so a face without the forms looks exactly as it did.
//
// Pure: no font, no panel. Both the drawing and the measuring go through it,
// so a line is measured as it is drawn.
#pragma once
#include <stdint.h>

namespace thaishape {

// The consonant the marks are stacking on, and whether the first storey above
// it is taken yet.
struct State {
  uint32_t base = 0;
  bool upper = false;
};

inline bool consonant(uint32_t cp) { return cp >= 0x0E01 && cp <= 0x0E2E; }
// Tall consonants: their tail rises into the mark zone, so marks move left.
inline bool tall(uint32_t cp) { return cp == 0x0E1B || cp == 0x0E1D || cp == 0x0E1F || cp == 0x0E2C; }
inline bool lowerVowel(uint32_t cp) { return cp >= 0x0E38 && cp <= 0x0E3A; }
inline bool tone(uint32_t cp) { return cp >= 0x0E48 && cp <= 0x0E4C; }  // incl. the thanthakhat
inline bool firstStorey(uint32_t cp) {
  return cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E37) || cp == 0x0E47 || cp == 0x0E4D;
}

// Upper vowels moved left, for a tall consonant.
inline uint32_t upperLeft(uint32_t cp) {
  switch (cp) {
    case 0x0E31: return 0xF710;  // mai han-akat
    case 0x0E34: return 0xF701;  // sara i
    case 0x0E35: return 0xF702;  // sara ii
    case 0x0E36: return 0xF703;  // sara ue
    case 0x0E37: return 0xF704;  // sara uee
    case 0x0E47: return 0xF712;  // maitaikhu
    case 0x0E4D: return 0xF711;  // nikhahit
  }
  return cp;
}

// The forms codepoint `cp` draws as, given the one after it. Writes one or two
// codepoints to out[] -- two only for sara am split so its ring can move left
// -- returns how many, and moves the state on. Call it for EVERY codepoint of
// a line, whichever face ends up drawing it, so a mark never stacks on a
// consonant from an earlier word.
inline int forms(State& s, uint32_t cp, uint32_t next, uint32_t out[2]) {
  out[0] = cp;
  int n = 1;
  if (cp < 0x0E01 || cp > 0x0E5B) {
    s.base = 0;
    s.upper = false;
    return n;
  }
  if (consonant(cp)) {
    s.base = cp;
    s.upper = false;
    // ญ and ฐ lose the tail a lower vowel would sit on.
    if (lowerVowel(next)) {
      if (cp == 0x0E0D) out[0] = 0xF70F;
      if (cp == 0x0E10) out[0] = 0xF700;
    }
    return n;
  }
  if (!s.base) return n;  // a mark with nothing under it: as it comes
  if (firstStorey(cp)) {
    if (tall(s.base)) out[0] = upperLeft(cp);
    s.upper = true;
  } else if (tone(cp)) {
    // Second storey over an upper vowel -- or over sara am's ring, which comes
    // after the tone in the text but sits under it on the page.
    const bool high = s.upper || next == 0x0E33;
    const uint32_t i = cp - 0x0E48;
    if (tall(s.base)) out[0] = (high ? 0xF713 : 0xF705) + i;
    else if (!high) out[0] = 0xF70A + i;
  } else if (lowerVowel(cp)) {
    if (s.base == 0x0E0E || s.base == 0x0E0F) out[0] = 0xF718 + (cp - 0x0E38);
  } else if (cp == 0x0E33) {
    // Sara am is a ring plus sara aa. On a tall consonant the ring has to move
    // left like any upper mark, which means drawing the two halves apart.
    if (tall(s.base)) {
      out[0] = 0xF711;
      out[1] = 0x0E32;
      n = 2;
    }
    s.base = 0;  // sara aa takes an advance: the cluster is over
  } else if (cp >= 0x0E30 && cp <= 0x0E46 && cp != 0x0E31 && !lowerVowel(cp)) {
    s.base = 0;  // a following vowel or sign with its own advance
  }
  return n;
}

}  // namespace thaishape
