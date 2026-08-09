// The little Unicode the firmware needs: UTF-8 stepping and line-break classes.
//
// Notes and flashcards carry whatever a phone keyboard can type -- Thai, Chinese,
// Korean, kana, accented Latin. This header owns the two questions every caller
// asks of such text: "what is the next codepoint" and "may a line break here".
// It is pure logic with no display types, so the core and both hosts share it.
#pragma once
#include <stdint.h>

namespace uni {

// Decodes the codepoint at p and advances p past it. Malformed bytes come back
// as one '?' per byte rather than desynchronising the walk -- on a fridge note
// a mangled character beats a mangled paragraph.
inline uint32_t next(const char*& p) {
  const uint8_t b0 = (uint8_t)*p;
  if (b0 < 0x80) {
    p++;
    return b0;
  }
  int n = 0;
  uint32_t cp = 0;
  if ((b0 & 0xE0) == 0xC0) {
    n = 1;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    n = 2;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    n = 3;
    cp = b0 & 0x07;
  } else {
    p++;
    return '?';
  }
  p++;
  for (int i = 0; i < n; i++) {
    if (((uint8_t)*p & 0xC0) != 0x80) return '?';
    cp = (cp << 6) | ((uint8_t)*p & 0x3F);
    p++;
  }
  return cp;
}

// Characters, not bytes. Anywhere a length decides how big to draw something,
// this is the length that matters: "หมูกรอบ" is seven characters and twenty-one
// bytes, and choosing a text size from the byte count shrinks every non-Latin
// string for no reason.
inline int count(const char* s) {
  int n = 0;
  for (const char* p = s; *p; n++) next(p);
  return n;
}

// --- Thai ---------------------------------------------------------------

inline bool thai(uint32_t cp) { return cp >= 0x0E01 && cp <= 0x0E5B; }

// Zero-advance combining marks: upper vowels, lower vowels, tone marks.
inline bool thaiMark(uint32_t cp) {
  return cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E3A) || (cp >= 0x0E47 && cp <= 0x0E4E);
}

// Marks that occupy the first storey above the consonant. A tone mark baked to
// sit above one of these is at the right height; over a bare consonant it must
// come down by the face's measured tone drop.
inline bool thaiUpper(uint32_t cp) {
  return cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E37) || cp == 0x0E47 || cp == 0x0E4D ||
         cp == 0x0E4E;
}

inline bool thaiTone(uint32_t cp) { return cp >= 0x0E48 && cp <= 0x0E4C; }

// --- CJK ----------------------------------------------------------------

// Scripts where any character boundary is a legal break: han, kana, hangul,
// CJK punctuation and fullwidth forms.
inline bool cjk(uint32_t cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFF65) ||
         (cp >= 0x3000 && cp <= 0x30FF);
}

// Kinsoku, the short version: the marks that must not begin a line.
inline bool noLineStart(uint32_t cp) {
  switch (cp) {
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0x30FB:  // ・
    case 0x30FC:  // ー
    case 0xFF01:  // ！
    case 0xFF09:  // ）
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0xFF1A:  // ：
    case 0xFF1B:  // ；
    case 0xFF1F:  // ？
    case 0x3009:
    case 0x300B:
    case 0x300D:
    case 0x300F:
    case 0x3011: return true;
    default: return false;
  }
}

// Thai characters a line must not start with, even though they have advances:
// sara am and the repetition/abbreviation signs read as part of what precedes.
inline bool thaiNoLineStart(uint32_t cp) {
  return cp == 0x0E33 || cp == 0x0E45 || cp == 0x0E46 || cp == 0x0E2F;
}

// May a line break fall between prev and cp? Spaces are handled by the caller;
// this is the spaceless-script rule. CJK breaks at any boundary that does not
// strand a closing mark; Thai breaks before any non-mark character. (True Thai
// segmentation needs a dictionary; breaking at cluster boundaries is the
// standard embedded fallback, and reads acceptably if not perfectly.)
inline bool breakBefore(uint32_t prev, uint32_t cp) {
  if (prev == 0) return false;
  if (cjk(cp)) return !noLineStart(cp);
  if (cjk(prev)) return true;  // between an ideograph and anything latin
  if (thai(cp) && thai(prev)) return !thaiMark(cp) && !thaiNoLineStart(cp);
  return false;
}

}  // namespace uni
