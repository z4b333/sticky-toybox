// A small on-screen keyboard (v2), for the few words the device itself asks
// for -- a calendar item's title. Latin letters, digits and a little
// punctuation; longer writing still comes from the phone.
//
// Four rows across the bottom of a portrait screen. The caller owns the text;
// this draws, hit-tests, and applies one key at a time.
#pragma once
#include <string.h>

#include "tools_ui.h"

namespace osk {

inline constexpr int ROWS = 4, KEY_H = 64, GAP = 4;
inline constexpr int TOP = SCREEN_H - ROWS * (KEY_H + GAP) - 8;  // 520

// Two layers: letters and symbols. Row 4 is the same on both.
inline const char* const LETTERS[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
inline const char* const SYMBOLS[3] = {"1234567890", "-/:;()&@'", ".,?!#%+"};

enum Special : int { K_NONE = -1, K_SHIFT = -2, K_BACK = -3, K_LAYER = -4, K_SPACE = -5, K_DONE = -6 };

struct State {
  bool shift = true;  // a title starts with a capital
  bool caps = false;  // shift tapped twice in a row: capitals until tapped again
  bool symbols = false;
  uint32_t lastShiftMs = 0;
};

// Shift drawn as an arrow (outlined; filled while on; with a bar under it for
// caps lock), backspace as a key-shaped tag with a cross -- the two shapes a
// phone has taught everyone, rather than the "^" and "<x" they replace.
inline void shiftIcon(ToolsCanvas& c, const TRect& r, bool on, bool caps, bool ink) {
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2 - (caps ? 4 : 0);
  const int top = cy - 14, mid = cy, bot = cy + 12;
  // Head (triangle) and stem.
  if (on) {
    for (int y = top; y <= mid; y++) {
      const int half = (y - top) * 14 / (mid - top);
      c.fillRect(cx - half, y, 2 * half + 1, 1, ink);
    }
    c.fillRect(cx - 6, mid, 13, bot - mid, ink);
  } else {
    c.drawLine(cx, top, cx - 14, mid, 2, ink);
    c.drawLine(cx, top, cx + 14, mid, 2, ink);
    c.drawLine(cx - 14, mid, cx - 6, mid, 2, ink);
    c.drawLine(cx + 6, mid, cx + 14, mid, 2, ink);
    c.drawLine(cx - 6, mid, cx - 6, bot, 2, ink);
    c.drawLine(cx + 6, mid, cx + 6, bot, 2, ink);
    c.drawLine(cx - 6, bot, cx + 6, bot, 2, ink);
  }
  if (caps) c.fillRect(cx - 8, bot + 5, 17, 3, ink);
}
inline void backIcon(ToolsCanvas& c, const TRect& r, bool ink) {
  const int cx = r.x + r.w / 2 + 3, cy = r.y + r.h / 2;
  const int L = cx - 22, R = cx + 16, T = cy - 12, B = cy + 12;
  c.drawLine(L, cy, L + 10, T, 2, ink);
  c.drawLine(L, cy, L + 10, B, 2, ink);
  c.drawLine(L + 10, T, R, T, 2, ink);
  c.drawLine(L + 10, B, R, B, 2, ink);
  c.drawLine(R, T, R, B, 2, ink);
  c.drawLine(cx - 5, cy - 6, cx + 7, cy + 6, 2, ink);
  c.drawLine(cx - 5, cy + 6, cx + 7, cy - 6, 2, ink);
}

// The key's rect and what it types (a char >= 32, or a Special).
inline bool keyAt(const State& st, int row, int col, TRect& r, int& code) {
  const int y = TOP + row * (KEY_H + GAP);
  const int W = SCREEN_W - 8;
  if (row < 3) {
    const char* keys = (st.symbols ? SYMBOLS : LETTERS)[row];
    const int n = (int)strlen(keys);
    const int kw = (W - 9 * GAP) / 10;
    // Row 3 has shift on the left and backspace on the right.
    const int extra = row == 2 ? 1 : 0;
    const int count = n + 2 * extra;
    if (col >= count) return false;
    const int rowW = count * kw + (count - 1) * GAP + (row == 2 ? kw / 2 * 2 : 0);
    int x = 4 + (W - rowW) / 2;
    for (int i = 0; i < count; i++) {
      const bool wide = row == 2 && (i == 0 || i == count - 1);
      const int w = wide ? kw + kw / 2 : kw;
      if (i == col) {
        r = TRect{x, y, w, KEY_H};
        if (row == 2 && i == 0) code = K_SHIFT;
        else if (row == 2 && i == count - 1) code = K_BACK;
        else {
          char ch = keys[i - extra];
          if ((st.shift || st.caps) && !st.symbols && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
          code = (unsigned char)ch;
        }
        return true;
      }
      x += w + GAP;
    }
    return false;
  }
  // Bottom row: 123/abc, space, Done.
  const int w0 = 96, w2 = 112, w1 = W - w0 - w2 - 2 * GAP;
  const int xs[3] = {4, 4 + w0 + GAP, 4 + w0 + GAP + w1 + GAP};
  const int ws[3] = {w0, w1, w2};
  if (col > 2) return false;
  r = TRect{xs[col], y, ws[col], KEY_H};
  code = col == 0 ? K_LAYER : (col == 1 ? K_SPACE : K_DONE);
  return true;
}

inline void draw(ToolsCanvas& c, const State& st) {
  c.fillRect(0, TOP - 8, SCREEN_W, SCREEN_H - TOP + 8, false);
  c.fillRect(0, TOP - 8, SCREEN_W, 2, true);
  for (int row = 0; row < ROWS; row++)
    for (int col = 0;; col++) {
      TRect r;
      int code;
      if (!keyAt(st, row, col, r, code)) break;
      const bool dark = code == K_DONE;
      if (dark) c.fillRect(r.x, r.y, r.w, r.h, true);
      else c.drawRect(r.x, r.y, r.w, r.h, 2, true);
      if (code == K_SHIFT) {
        shiftIcon(c, r, st.shift || st.caps, st.caps, true);
        continue;
      }
      if (code == K_BACK) {
        backIcon(c, r, true);
        continue;
      }
      char lab[8];
      switch (code) {
        case K_LAYER: snprintf(lab, sizeof(lab), st.symbols ? "abc" : "123"); break;
        case K_SPACE: snprintf(lab, sizeof(lab), "space"); break;
        case K_DONE: snprintf(lab, sizeof(lab), "Done"); break;
        default: lab[0] = (char)code; lab[1] = 0; break;
      }
      c.textInBox(r.x, r.y, r.w, r.h, lab, TS_MED, !dark, code < 0);
    }
}

// Applies a tap. Returns the Special it was (K_DONE for the caller to act
// on), the char typed, or K_NONE when it missed every key.
inline int tap(State& st, char* text, int cap, int x, int y, uint32_t nowMs = 0) {
  if (y < TOP) return K_NONE;
  for (int row = 0; row < ROWS; row++)
    for (int col = 0;; col++) {
      TRect r;
      int code;
      if (!keyAt(st, row, col, r, code)) break;
      if (!r.hit(x, y)) continue;
      const int n = (int)strlen(text);
      switch (code) {
        case K_SHIFT:
          // Twice quickly locks capitals; once more lets them go.
          if (st.caps) {
            st.caps = false;
            st.shift = false;
          } else if (st.shift && nowMs && nowMs - st.lastShiftMs < 600) {
            st.caps = true;
          } else {
            st.shift = !st.shift;
          }
          st.lastShiftMs = nowMs;
          break;
        case K_LAYER: st.symbols = !st.symbols; break;
        case K_BACK:
          if (n > 0) text[n - 1] = 0;
          if (n <= 1) st.shift = true;
          break;
        case K_SPACE:
          if (n > 0 && n < cap - 1 && text[n - 1] != ' ') {
            text[n] = ' ';
            text[n + 1] = 0;
          }
          break;
        case K_DONE: break;
        default:
          if (n < cap - 1) {
            text[n] = (char)code;
            text[n + 1] = 0;
          }
          st.shift = false;  // one capital, like a phone
          break;
      }
      return code;
    }
  return K_NONE;
}

}  // namespace osk
