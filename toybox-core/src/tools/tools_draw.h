// Big seven-segment digits and card-suit shapes, drawn from canvas primitives
// only — so they render identically (and at any size) in both firmwares.
// Neither host has a font large enough for a 140 px countdown, hence this.
#pragma once
#include "tools_ui.h"

namespace tdraw {

// A rounded rectangle, built from straight bands and four corner discs -- the
// canvas has circles and rectangles but no arcs, and this is what you can make
// from those. Filled when thickness is 0; otherwise the inside is painted back
// out, which leaves a ring of even width all the way round the corners.
inline void fillRound(ToolsCanvas& c, int x, int y, int w, int h, int r, bool black) {
  if (r < 0) r = 0;
  if (2 * r > w) r = w / 2;
  if (2 * r > h) r = h / 2;
  c.fillRect(x + r, y, w - 2 * r, h, black);
  c.fillRect(x, y + r, r, h - 2 * r, black);
  c.fillRect(x + w - r, y + r, r, h - 2 * r, black);
  c.fillCircle(x + r, y + r, r, black);
  c.fillCircle(x + w - 1 - r, y + r, r, black);
  c.fillCircle(x + r, y + h - 1 - r, r, black);
  c.fillCircle(x + w - 1 - r, y + h - 1 - r, r, black);
}

inline void roundRect(ToolsCanvas& c, int x, int y, int w, int h, int r, int t, bool black) {
  if (t <= 0) {
    fillRound(c, x, y, w, h, r, black);
    return;
  }
  fillRound(c, x, y, w, h, r, black);
  fillRound(c, x + t, y + t, w - 2 * t, h - 2 * t, r - t < 1 ? 1 : r - t, !black);
}


// --- seven-segment numerals --------------------------------------------------
// Segment bit order: a=1 b=2 c=4 d=8 e=16 f=32 g=64
inline uint8_t seg7Mask(char ch) {
  switch (ch) {
    case '0': return 0x3F;
    case '1': return 0x06;
    case '2': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': return 0x6F;
    case '-': return 0x40;
    default: return 0x00;
  }
}

inline int seg7DigitW(int h) { return (h * 3) / 5; }
inline int seg7Thick(int h) { return h / 8 < 2 ? 2 : h / 8; }
inline int seg7Gap(int h) { return h / 7 < 2 ? 2 : h / 7; }

// Width of one character cell, including its trailing gap.
inline int seg7CharAdvance(char ch, int h) {
  const int t = seg7Thick(h);
  const int gap = seg7Gap(h);
  if (ch == ':') return t + gap;
  if (ch == ' ') return seg7DigitW(h) / 2 + gap;
  if (ch == '+') return seg7DigitW(h) + gap;
  return seg7DigitW(h) + gap;
}

inline int seg7Width(const char* s, int h) {
  int w = 0;
  for (const char* p = s; *p; ++p) w += seg7CharAdvance(*p, h);
  return w > 0 ? w - seg7Gap(h) : 0;
}

inline void seg7Digit(ToolsCanvas& c, int x, int y, int h, char ch, bool black) {
  const int w = seg7DigitW(h);
  const int t = seg7Thick(h);
  const int half = (h - 3 * t) / 2;

  if (ch == ':') {
    c.fillRect(x, y + h / 3 - t / 2, t, t, black);
    c.fillRect(x, y + (2 * h) / 3 - t / 2, t, t, black);
    return;
  }
  if (ch == '+') {  // plus sign, sized to match the digits
    c.fillRect(x, y + h / 2 - t / 2, w, t, black);
    c.fillRect(x + w / 2 - t / 2, y + h / 2 - w / 2, t, w, black);
    return;
  }
  const uint8_t m = seg7Mask(ch);
  if (m & 0x01) c.fillRect(x + t, y, w - 2 * t, t, black);                       // a
  if (m & 0x02) c.fillRect(x + w - t, y + t, t, half, black);                    // b
  if (m & 0x04) c.fillRect(x + w - t, y + 2 * t + half, t, half, black);         // c
  if (m & 0x08) c.fillRect(x + t, y + h - t, w - 2 * t, t, black);               // d
  if (m & 0x10) c.fillRect(x, y + 2 * t + half, t, half, black);                 // e
  if (m & 0x20) c.fillRect(x, y + t, t, half, black);                            // f
  if (m & 0x40) c.fillRect(x + t, y + t + half, w - 2 * t, t, black);            // g
}

inline void seg7Text(ToolsCanvas& c, int x, int y, int h, const char* s, bool black) {
  int cx = x;
  for (const char* p = s; *p; ++p) {
    seg7Digit(c, cx, y, h, *p, black);
    cx += seg7CharAdvance(*p, h);
  }
}

inline void seg7Centered(ToolsCanvas& c, int cx, int y, int h, const char* s, bool black) {
  seg7Text(c, cx - seg7Width(s, h) / 2, y, h, s, black);
}

// --- card suits --------------------------------------------------------------
// `size` is the full width/height of the symbol's bounding box.

inline void diamond(ToolsCanvas& c, int cx, int cy, int size, bool black) {
  const int half = size / 2;
  for (int dy = -half; dy <= half; ++dy) {
    const int w = half - (dy < 0 ? -dy : dy);
    if (w > 0) c.fillRect(cx - w, cy + dy, 2 * w, 1, black);
  }
}

inline void heart(ToolsCanvas& c, int cx, int cy, int size, bool black) {
  const int r = size / 4;
  c.fillCircle(cx - r, cy - r / 2, r, black);
  c.fillCircle(cx + r, cy - r / 2, r, black);
  // lower triangle from the lobes down to the point
  const int top = cy - r / 2;
  const int bottom = cy + size / 2;
  const int span = bottom - top;
  for (int i = 0; i <= span; ++i) {
    const int w = (2 * r) - (2 * r * i) / span;
    if (w > 0) c.fillRect(cx - w, top + i, 2 * w, 1, black);
  }
}

inline void spade(ToolsCanvas& c, int cx, int cy, int size, bool black) {
  const int r = size / 4;
  // inverted heart
  c.fillCircle(cx - r, cy + r / 2, r, black);
  c.fillCircle(cx + r, cy + r / 2, r, black);
  const int bot = cy + r / 2;
  const int top = cy - size / 2;
  const int span = bot - top;
  for (int i = 0; i <= span; ++i) {
    const int w = (2 * r) - (2 * r * i) / span;
    if (w > 0) c.fillRect(cx - w, bot - i, 2 * w, 1, black);
  }
  // stem
  const int sw = size / 6;
  c.fillRect(cx - sw / 2, cy + r, sw, size / 3, black);
  c.fillRect(cx - sw, cy + r + size / 3 - sw / 2, sw * 2, sw, black);
}

inline void club(ToolsCanvas& c, int cx, int cy, int size, bool black) {
  const int r = size / 4;
  c.fillCircle(cx, cy - r, r, black);
  c.fillCircle(cx - r, cy + r / 2, r, black);
  c.fillCircle(cx + r, cy + r / 2, r, black);
  // The three lobes are tangent, not overlapping — close the seam between them.
  c.fillRect(cx - r / 2, cy - r, r, r + r / 2, black);
  const int sw = size / 6;
  c.fillRect(cx - sw / 2, cy + r / 2, sw, size / 3, black);
  c.fillRect(cx - sw, cy + r / 2 + size / 3 - sw / 2, sw * 2, sw, black);
}

// suit: 0 spade, 1 heart, 2 diamond, 3 club
inline void suit(ToolsCanvas& c, int idx, int cx, int cy, int size, bool black) {
  switch (idx) {
    case 0: spade(c, cx, cy, size, black); break;
    case 1: heart(c, cx, cy, size, black); break;
    case 2: diamond(c, cx, cy, size, black); break;
    default: club(c, cx, cy, size, black); break;
  }
}

inline const char* rankName(int rank) {  // 0..12 -> A,2..10,J,Q,K
  static const char* kNames[13] = {"A", "2", "3", "4",  "5", "6", "7",
                                   "8", "9", "10", "J", "Q", "K"};
  return kNames[rank];
}

// --- misc --------------------------------------------------------------------

// Horizontal progress bar, 0..1000 permille (integer maths only).
inline void progressBar(ToolsCanvas& c, int x, int y, int w, int h, int permille) {
  c.drawRect(x, y, w, h, 2, true);
  if (permille < 0) permille = 0;
  if (permille > 1000) permille = 1000;
  const int inner = ((w - 8) * permille) / 1000;
  if (inner > 0) c.fillRect(x + 4, y + 4, inner, h - 8, true);
}

// Standard D6 pip face.
inline void dicePips(ToolsCanvas& c, int x, int y, int size, int value, bool onBlack) {
  const int r = size / 12;
  const int a = x + size / 4, b = x + size / 2, d = x + (3 * size) / 4;
  const int p = y + size / 4, q = y + size / 2, s = y + (3 * size) / 4;
  const bool ink = !onBlack;
  auto dot = [&](int cx, int cy) { c.fillCircle(cx, cy, r, ink); };
  if (value & 1) dot(b, q);                       // centre for odd values
  if (value >= 2) { dot(a, p); dot(d, s); }
  if (value >= 4) { dot(d, p); dot(a, s); }
  if (value >= 6) { dot(a, q); dot(d, q); }
}

}  // namespace tdraw
