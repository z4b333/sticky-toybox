// The charging dashboard (C6, kit "Charging dashboard" rows).
//
// Shown while the Sticky is on USB power and awake: the date and a big clock,
// the weather outside (Open-Meteo, the same place as Climate), the room, and a
// small battery line. Touch does nothing here; any button sleeps (main.cpp).
// Laid out for whichever way the canvas stands: 480x800 portrait or 800x480
// landscape. The popup that leads in is landscape only (owner, 25 Nov).
//
// Pure drawing: main.cpp gathers the Info and owns the timing, so the host
// harness renders every state of it.
#pragma once
#include <stdio.h>
#include <string.h>

#include "../chrome.h"
#include "big_digits.h"
#include "lock_image.h"
#include "tools_ui.h"
#include "weather.h"

namespace chgdash {

// Labels (date, OUTSIDE, INSIDE, BATTERY): the medium face in bold, lightly
// tracked -- the small tracked capitals were hard to read on glass (owner,
// 26 Nov).
inline constexpr TSize LBL = TS_MED;

enum Batt : uint8_t { B_CHARGING, B_HELD, B_FULL, B_USB };

struct Info {
  bool haveClock = false;
  int hh = 0, mm = 0, year = 0, month = 0, day = 0;
  int pct = -1;               // -1: no gauge
  Batt batt = B_CHARGING;
  bool noPlace = false;       // no weather place set, or no home Wi-Fi saved
  bool haveWx = false;
  wx::Now now{};
  char place[wx::CITY_MAX] = {};
  bool haveIndoor = false;
  int inDeci = 0, inRh = 0;   // already offset-corrected
  bool fahrenheit = false;
  // The picture beside the weather (C6 setting): a 480x800 file in flash,
  // 1-bit (tbimg) or, for a lock picture sent from the card, grey (tbg2).
  // nullptr: none, and the weather has the space.
  const char* pic = nullptr;
  bool picGrey = false;
  bool picBorder = true;  // Settings > Dashboard > Picture border
};

inline int toUnit(int deciC, bool f) { return f ? (deciC * 18 + 3200) / 10 : deciC; }
inline int roundDeci(int d) { return d >= 0 ? (d + 5) / 10 : -((-d + 5) / 10); }

// --- marks -------------------------------------------------------------------
inline void bolt(ToolsCanvas& c, int x, int y, int s, bool black = true) {
  // A lightning bolt in an s x s box: two slanted bars meeting at the middle.
  const int t = s / 6 > 2 ? s / 6 : 2;
  c.drawLine(x + s * 2 / 3, y, x + s / 4, y + s / 2 + 1, t, black);
  c.drawLine(x + s / 4, y + s / 2, x + s * 3 / 4, y + s / 2, t, black);
  c.drawLine(x + s * 3 / 4, y + s / 2 - 1, x + s / 3, y + s, t, black);
}
inline void padlock(ToolsCanvas& c, int x, int y, int s) {
  c.drawCircle(x + s / 2, y + s * 2 / 5, s / 4, 3, true);
  c.fillRect(x + s / 6, y + s * 2 / 5, s * 2 / 3, s * 3 / 5, true);
}
inline void sun(ToolsCanvas& c, int cx, int cy, int r) {
  c.drawCircle(cx, cy, r, 3, true);
  static const int8_t D[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
  for (const auto& d : D) {
    const float k = (d[0] && d[1]) ? 0.7071f : 1.f;
    const int a = (int)((r + 6) * k), b = (int)((r + 13) * k);
    c.drawLine(cx + d[0] * a, cy + d[1] * a, cx + d[0] * b, cy + d[1] * b, 3, true);
  }
}
inline void cloud(ToolsCanvas& c, int x, int y, int w) {
  // Three humps over a flat base, outlined, white inside so it can sit on a sun.
  const int h = w * 11 / 20, r1 = h * 2 / 5, r2 = h / 2, r3 = h * 3 / 10;
  const int by = y + h;
  c.fillCircle(x + r1, by - r1, r1, false);
  c.fillCircle(x + w * 9 / 20, by - r2 - r1 / 2, r2, false);
  c.fillCircle(x + w - r3, by - r3, r3, false);
  c.fillRect(x + r1, by - r1, w - r1 - r3, r1, false);
  c.drawCircle(x + r1, by - r1, r1, 3, true);
  c.drawCircle(x + w * 9 / 20, by - r2 - r1 / 2, r2, 3, true);
  c.drawCircle(x + w - r3, by - r3, r3, 3, true);
  // Wipe the inner arcs so only the outline remains.
  c.fillCircle(x + r1, by - r1, r1 - 3, false);
  c.fillCircle(x + w * 9 / 20, by - r2 - r1 / 2, r2 - 3, false);
  c.fillCircle(x + w - r3, by - r3, r3 - 3, false);
  c.fillRect(x + r1, by - r1 - 2, w - r1 - r3, r1 - 1, false);
  c.fillRect(x + r1, by - 3, w - r1 - r3, 3, true);
}
// A weather mark for a WMO code in an s x s box.
inline void wxIcon(ToolsCanvas& c, int x, int y, int s, int code) {
  const bool clear = code <= 1, part = code == 2;
  if (clear) {
    sun(c, x + s / 2, y + s / 2, s / 5);
    return;
  }
  if (part) sun(c, x + s * 2 / 5, y + s * 2 / 5, s / 7);
  const int cw = s * 3 / 4, cx = part ? x + s / 5 : x + s / 8, cy = part ? y + s * 2 / 5 : y + s / 5;
  cloud(c, cx, cy, cw);
  const int below = cy + cw * 11 / 20 + 8;
  if (code == 45 || code == 48) {  // fog: three bars
    for (int k = 0; k < 3; k++) c.fillRect(x + s / 8 + k * 4, below + k * 8, cw - k * 8, 3, true);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {  // snow: dots
    for (int k = 0; k < 3; k++) c.fillCircle(cx + cw / 4 + k * cw / 4, below + 6 + (k & 1) * 8, 4, true);
  } else if (code >= 95) {  // storm: a bolt under the cloud
    bolt(c, cx + cw / 3, below - 4, s / 4);
  } else if (code >= 51) {  // drizzle, rain, showers: slanted strokes
    for (int k = 0; k < 3; k++) {
      const int sx = cx + cw / 4 + k * cw / 4;
      c.drawLine(sx, below, sx - 6, below + 14, 3, true);
    }
  }
}

// The big numerals at num/den of their size, sampled nearest, left edge x.
inline int bigWidth(const char* s, int num, int den) { return bigdigits::width(s) * num / den; }
inline void bigScaled(ToolsCanvas& c, int x, int y, const char* s, int num, int den) {
  if (num == den) {
    bigdigits::drawCentered(c, x + bigdigits::width(s) / 2, y, s, true);
    return;
  }
  const int oh = bigdigits::H * num / den;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') continue;
    const int d = *s - '0', dw = bigdigits::W[d], stride = (dw + 7) / 8, ow = dw * num / den;
    const uint8_t* bits = bigdigits::BITS + bigdigits::OFF[d];
    for (int oy = 0; oy < oh; oy++) {
      const uint8_t* row = bits + (oy * den / num) * stride;
      int run = -1;
      for (int ox = 0; ox <= ow; ox++) {
        bool ink = false;
        if (ox < ow) {
          const int sx = ox * den / num;
          ink = pgm_read_byte(row + sx / 8) & (0x80 >> (sx & 7));
        }
        if (ink && run < 0) run = ox;
        if (!ink && run >= 0) {
          c.fillRect(x + run, y + oy, ox - run, 1, true);
          run = -1;
        }
      }
    }
    x += ow;
  }
}

// --- pieces --------------------------------------------------------------------
// The clock in the big numerals: HH, a colon of two squares, MM. Left edge at x.
inline int clockWidth(int num = 1, int den = 1) { return (bigdigits::width("0000") + 34) * num / den; }
inline void bigClock(ToolsCanvas& c, int x, int y, int hh, int mm, int num = 1, int den = 1) {
  char a[3], b[3];
  snprintf(a, sizeof(a), "%02d", hh);
  snprintf(b, sizeof(b), "%02d", mm);
  const int wa = bigdigits::width(a) * num / den, H = bigdigits::H * num / den;
  const int sq = 18 * num / den, gap = 34 * num / den;
  bigScaled(c, x, y, a, num, den);
  const int cx = x + wa + 6 * num / den;
  c.fillRect(cx, y + H * 30 / 100, sq, sq, true);
  c.fillRect(cx, y + H * 70 / 100, sq, sq, true);
  bigScaled(c, x + wa + gap, y, b, num, den);
}

inline void dateLine(const Info& in, char* out, size_t n) {
  out[0] = 0;
  if (!in.haveClock || in.month < 1 || in.month > 12) return;
  static const char* MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  static const char* WD[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const int T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  const int yy = in.year - (in.month < 3);
  const int wd = (yy + yy / 4 - yy / 100 + yy / 400 + T[in.month - 1] + in.day) % 7;
  snprintf(out, n, "%s %d %s", WD[wd], in.day, MON[in.month - 1]);
}

// The outside temperature as "31°": big numerals (at num/den) when they fit
// in maxW, the text face when they do not.
inline void temp(ToolsCanvas& c, int x, int y, int deciC, bool f, int maxW, int num = 1,
                 int den = 1) {
  const int v = roundDeci(toUnit(deciC, f));
  char nb[8];
  snprintf(nb, sizeof(nb), "%d", v < 0 ? -v : v);
  const int minus = v < 0 ? 36 * num / den : 0;
  const int deg = 34 * num / den;  // a drawn ring: the font's degree sign is too slight
  if (minus + bigWidth(nb, num, den) + deg <= maxW) {
    const int H = bigdigits::H * num / den;
    if (minus) c.fillRect(x, y + H / 2 - 6, minus - 10, 12, true);
    const int w = bigWidth(nb, num, den);
    bigScaled(c, x + minus, y, nb, num, den);
    const int r = 13 * num / den < 7 ? 7 : 13 * num / den;
    const int rx = x + minus + w + 10 * num / den, ry = y + 22 * num / den;
    c.fillCircle(rx, ry, r, true);
    c.fillCircle(rx, ry, r * 7 / 13, false);
  } else {
    char s[12];
    snprintf(s, sizeof(s), "%d\xc2\xb0", v);
    c.text(x, y, s, TS_HUGE, true, true);
  }
}

inline const char* battLabel(const Info& in) {
  switch (in.batt) {
    case B_HELD: return "Held at 80% \xc2\xb7 battery care";
    case B_FULL: return "Full";
    case B_USB: return "On USB power";
    default: return "Charging";
  }
}

// Kit section label: small tracked capitals over a 2 px rule. Returns the
// y just under the rule.
inline int section(ToolsCanvas& c, int x, int y, int w, const char* label) {
  c.textTracked(x, y, label, LBL, true, true, 2);
  const int ry = y + c.textHeight(LBL) + 6;
  c.fillRect(x, ry, w, 2, true);
  return ry + 2;
}
// The kit's battery cell (the banners' BI_BATTERY): 56 x 28, a nub, filled
// to the level.
inline void cell(ToolsCanvas& c, int x, int y, int pct) {
  c.drawRect(x, y, 56, 28, 3, true);
  c.fillRect(x + 56, y + 8, 5, 12, true);
  const int fill = pct < 0 ? 0 : (48 * (pct > 100 ? 100 : pct) + 50) / 100;
  if (fill > 0) c.fillRect(x + 4, y + 4, fill < 4 ? 4 : fill, 20, true);
}
// BATTERY section from y down, w wide. Returns the bottom edge.
inline int sectionHeight(const ToolsCanvas& c) {
  return c.textHeight(LBL) + 8 + 12 + c.textHeight(TS_HUGE);
}
inline int battery(ToolsCanvas& c, const Info& in, int x, int y, int w) {
  y = section(c, x, y, w, "BATTERY") + 12;
  const int th = c.textHeight(TS_HUGE);
  const char* st = battLabel(in);
  const int sw = c.textWidth(st, TS_MED);
  const int sy = y + (th - c.textHeight(TS_MED)) / 2;
  if (in.pct < 0) {
    c.text(x, sy, st, TS_MED, true);
    return y + th;
  }
  cell(c, x, y + (th - 28) / 2, in.pct);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", in.pct);
  c.text(x + 61 + 18, y, pct, TS_HUGE, true, true);
  const int pw = c.textWidth(pct, TS_HUGE, true);
  const int room = w - (61 + 18 + pw + 16);
  if (sw <= room) c.text(x + w - sw, sy, st, TS_MED, true);
  else c.textClipped(x + 61 + 18 + pw + 16, sy, room, st, TS_SMALL, true);
  return y + th;
}

inline void outsideLabel(const Info& in, char* lab, size_t n, int w) {
  // needs a canvas to measure; trimmed by the callers' section() width below
  const bool show = in.haveWx && !in.noPlace;
  if (show && in.place[0]) {
    char up[wx::CITY_MAX];
    size_t i = 0;
    for (; in.place[i] && i + 1 < sizeof(up); i++) {
      const char ch = in.place[i];
      up[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
    }
    up[i] = 0;
    snprintf(lab, n, "OUTSIDE \xc2\xb7 %s", up);
  } else {
    snprintf(lab, n, "OUTSIDE");
  }
  // Roughly 13 px a tracked small capital: a long place is cut at the rule.
  const size_t maxc = (size_t)(w / 13);
  if (strlen(lab) > maxc && maxc > 8) lab[maxc] = 0;
}
// The OUTSIDE label: "OUTSIDE · MAE WANG" over the rule, the place cut
// when the line is short. (The Open-Meteo credit sits under the condition:
// see credit().)
inline int outsideSection(ToolsCanvas& c, const Info& in, int x, int y, int w) {
  char lab[wx::CITY_MAX + 16];
  outsideLabel(in, lab, sizeof(lab), 1000);
  const bool credit = false;
  const char* cr = "by Open-Meteo";
  const int crW = credit ? c.textWidth(cr, TS_SMALL) : 0;
  const int room = w - (credit ? crW + 14 : 0);
  while (c.textTrackedWidth(lab, LBL, true, 2) > room && strlen(lab) > 7) {
    size_t n = strlen(lab) - 1;
    // never leave half of a UTF-8 dot behind
    while (n > 0 && ((uint8_t)lab[n] & 0xC0) == 0x80) n--;
    lab[n] = 0;
  }
  const int ry = section(c, x, y, w, lab);
  if (credit) {
    const int ty = y + (c.textHeight(TS_SMALL) - c.textHeight(TS_SMALL)) / 2;
    c.text(x + w - crW, ty, cr, TS_SMALL, true);
  }
  return ry;
}

// "by Open-Meteo" under the condition (owner, 26 Nov), as its terms ask
// wherever a reading is shown. Returns the height used.
inline int credit(ToolsCanvas& c, int x, int y, int w) {
  c.textClipped(x, y, w, "by Open-Meteo", TS_SMALL, true);
  return c.textHeight(TS_SMALL);
}

inline void indoorText(const Info& in, char* s, size_t n) {
  const int v = toUnit(in.inDeci, in.fahrenheit);
  const int a = v < 0 ? -v : v;
  snprintf(s, n, "%s%d.%d\xc2\xb0 \xc2\xb7 %d%%", v < 0 ? "-" : "", a / 10, a % 10, in.inRh);
}

// The OUTSIDE section: label (with the place), icon and temperature,
// condition, a dotted rule, and the INSIDE row. Returns the bottom edge.
inline int weather(ToolsCanvas& c, const Info& in, int x, int y, int w, int iconS, int num = 1,
                   int den = 1) {
  const bool show = in.haveWx && !in.noPlace;
  y = outsideSection(c, in, x, y, w) + 12;
  if (show) {
    const int H = bigdigits::H * num / den;
    wxIcon(c, x - 4, y + (H - iconS) / 2, iconS, in.now.code);
    temp(c, x + iconS + 6, y, in.now.t, in.fahrenheit, w - iconS - 6, num, den);
    y += H + 8;
    c.textClipped(x, y, w, wx::codeWord(in.now.code), TS_LARGE, true, true);
    y += c.textHeight(TS_LARGE) + 2;
    y += credit(c, x, y, w);
  } else {
    c.text(x, y, in.noPlace ? "No weather yet" : "No reading yet", TS_LARGE, true, true);
    y += c.textHeight(TS_LARGE) + 6;
    if (in.noPlace) {
      c.textClipped(x, y, w, "Set a place in Climate,", TS_MED, true);
      y += c.textHeight(TS_MED) + 2;
      c.textClipped(x, y, w, "then join home Wi-Fi.", TS_MED, true);
    } else {
      c.textClipped(x, y, w, "will update on Wi-Fi", TS_MED, true);
    }
    y += c.textHeight(TS_MED);
  }
  if (in.haveIndoor) {
    y += 14;
    chrome2::dottedRule(c, x, y, w);
    y += 14;
    const int v = toUnit(in.inDeci, in.fahrenheit);
    const int a = v < 0 ? -v : v;
    char s[32];
    snprintf(s, sizeof(s), "%s%d.%d\xc2\xb0 \xc2\xb7 %d%%", v < 0 ? "-" : "", a / 10, a % 10, in.inRh);
    const int mh = c.textHeight(TS_MED);
    c.textTracked(x, y + (mh - c.textHeight(LBL)) / 2, "INSIDE", LBL, true, true, 2);
    c.text(x + w - c.textWidth(s, TS_MED, true), y, s, TS_MED, true, true);
    y += mh;
  }
  return y;
}

inline void footer(ToolsCanvas& c) {
  const int y = c.height() - 44;
  c.fillRect(0, y, c.width(), 1, true);
  const int ty = y + (44 - c.textHeight(TS_SMALL)) / 2;
  c.text(18, ty, "any button to sleep", TS_SMALL, true);
  // Owner, 25 Nov: the corner says what the page does on its own. The
  // Open-Meteo credit stays in Climate and on the phone page.
  const char* cr = "auto rotate every 10 s";
  c.text(c.width() - 18 - c.textWidth(cr, TS_SMALL), ty, cr, TS_SMALL, true);
}

// A 480x800 picture at num/den, shrunk properly (owner, 25 Nov: the nearest-
// pixel shrink turned a dithered photo into speckle). Each output pixel is
// the AVERAGE of the source pixels it covers -- which turns the stored dither
// back into grey -- and the greys are then dithered afresh at the new size
// (Floyd-Steinberg), so shading stays smooth and edges stay put.
// Reads 1-bit (tbimg, 1 = white) or 2-bit (tbg2, 0 black .. 3 white).
inline bool drawSmooth(ToolsCanvas& c, const char* path, bool grey, int x, int y, int num, int den) {
  size_t len = 0;
  char* buf = tfs::readAlloc(path, len);
  if (!buf) return false;
  const uint32_t want = grey ? tbg2::FILE_SIZE : tbimg::FILE_SIZE;
  if (len != want) {
    free(buf);
    return false;
  }
  const uint8_t* px = (const uint8_t*)buf + (grey ? tbg2::HEADER : tbimg::HEADER);
  const int SW = 480, SH = 800, ow = SW * num / den, oh = SH * num / den;
  // Error rows for the dither, one pixel of slack each side.
  int16_t* cur = (int16_t*)calloc(ow + 2, sizeof(int16_t));
  int16_t* nxt = (int16_t*)calloc(ow + 2, sizeof(int16_t));
  if (!cur || !nxt) {
    free(cur);
    free(nxt);
    free(buf);
    return false;
  }
  auto light = [&](int sx, int sy) -> int {  // 0 black .. 3 white
    if (grey) return (px[(size_t)sy * (SW / 4) + (sx >> 2)] >> ((3 - (sx & 3)) * 2)) & 3;
    return (px[(size_t)sy * (SW / 8) + (sx >> 3)] & (0x80 >> (sx & 7))) ? 3 : 0;
  };
  c.fillRect(x, y, ow, oh, false);
  for (int oy = 0; oy < oh; oy++) {
    const int sy0 = oy * den / num;
    int sy1 = (oy + 1) * den / num;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    int run = -1;
    for (int ox = 0; ox <= ow; ox++) {
      bool ink = false;
      if (ox < ow) {
        const int sx0 = ox * den / num;
        int sx1 = (ox + 1) * den / num;
        if (sx1 <= sx0) sx1 = sx0 + 1;
        int sum = 0, n = 0;
        for (int sy = sy0; sy < sy1 && sy < SH; sy++)
          for (int sx = sx0; sx < sx1 && sx < SW; sx++) {
            sum += light(sx, sy);
            n++;
          }
        // 0..255, with a touch more contrast: a shrunk photo reads flat.
        int v = n ? sum * 85 / n : 255;
        v = 128 + (v - 128) * 9 / 8;
        v += cur[ox + 1];
        const int out = v < 128 ? 0 : 255;
        ink = out == 0;
        const int e = v - out;
        cur[ox + 2] += (int16_t)(e * 7 / 16);
        nxt[ox] += (int16_t)(e * 3 / 16);
        nxt[ox + 1] += (int16_t)(e * 5 / 16);
        nxt[ox + 2] += (int16_t)(e / 16);
      }
      if (ink && run < 0) run = ox;
      if (!ink && run >= 0) {
        c.fillRect(x + run, y + oy, ox - run, 1, true);
        run = -1;
      }
    }
    int16_t* t = cur;
    cur = nxt;
    nxt = t;
    memset(nxt, 0, (ow + 2) * sizeof(int16_t));
  }
  free(cur);
  free(nxt);
  free(buf);
  return true;
}
inline bool picture(ToolsCanvas& c, const Info& in, int x, int y, int num, int den) {
  if (!in.pic) return false;
  const bool ok = drawSmooth(c, in.pic, in.picGrey, x, y, num, den);
  if (ok && in.picBorder) c.drawRect(x - 2, y - 2, 480 * num / den + 4, 800 * num / den + 4, 2, true);
  return ok;
}

// Portrait, with a picture (owner, 25 Nov: bigger): the picture at half size
// (240x400) on the left under the OUTSIDE label, the weather stacked in the
// narrow column beside it.
inline void weatherBeside(ToolsCanvas& c, const Info& in, int x, int y, int w) {
  y = outsideSection(c, in, x, y, w) + 14;
  picture(c, in, x + 2, y + 2, 1, 2);
  const int cx = x + 240 + 22, cw = x + w - cx;
  int cy = y + 4;
  if (in.haveWx && !in.noPlace) {
    wxIcon(c, cx - 4, cy, 56, in.now.code);
    cy += 64;
    temp(c, cx, cy, in.now.t, in.fahrenheit, cw, 1, 2);
    cy += bigdigits::H / 2 + 12;
    c.textClipped(cx, cy, cw, wx::codeWord(in.now.code), TS_MED, true, true);
    cy += c.textHeight(TS_MED) + 2;
    cy += credit(c, cx, cy, cw);
  } else {
    c.textClipped(cx, cy, cw, in.noPlace ? "No weather" : "No reading", TS_MED, true, true);
    cy += c.textHeight(TS_MED) + 4;
    c.textClipped(cx, cy, cw, in.noPlace ? "set a place" : "on Wi-Fi", TS_SMALL, true);
    c.textClipped(cx, cy + c.textHeight(TS_SMALL) + 2, cw, in.noPlace ? "in Climate" : "soon", TS_SMALL,
                  true);
    cy += 2 * c.textHeight(TS_SMALL) + 2;
  }
  if (in.haveIndoor) {
    cy += 16;
    chrome2::dottedRule(c, cx, cy, cw);
    cy += 14;
    c.textTracked(cx, cy, "INSIDE", LBL, true, true, 2);
    cy += c.textHeight(LBL) + 6;
    char s[32];
    indoorText(in, s, sizeof(s));
    c.textClipped(cx, cy, cw, s, TS_MED, true, true);
  }
}

// Landscape, with a picture: one row under the OUTSIDE label -- icon, the
// temperature at half size, condition over the room.
inline void weatherRow(ToolsCanvas& c, const Info& in, int x, int y, int w) {
  y = outsideSection(c, in, x, y, w) + 10;
  int tx = x;
  if (in.haveWx && !in.noPlace) {
    wxIcon(c, x - 4, y, 58, in.now.code);
    tx = x + 62;
    temp(c, tx, y, in.now.t, in.fahrenheit, 150, 1, 2);
    char nb[8];
    const int v = roundDeci(toUnit(in.now.t, in.fahrenheit));
    snprintf(nb, sizeof(nb), "%d", v < 0 ? -v : v);
    tx += (v < 0 ? 18 : 0) + bigWidth(nb, 1, 2) + 17 + 22;
    c.textClipped(tx, y, x + w - tx, wx::codeWord(in.now.code), TS_MED, true, true);
    credit(c, tx, y + c.textHeight(TS_MED), x + w - tx);
  } else {
    c.textClipped(tx, y + 2, w, in.noPlace ? "No weather yet" : "No reading yet", TS_MED, true, true);
  }
  if (in.haveIndoor) {
    char s[40], v[32];
    indoorText(in, v, sizeof(v));
    snprintf(s, sizeof(s), "Inside %s", v);
    c.textClipped(tx, y + c.textHeight(TS_MED) + c.textHeight(TS_SMALL) + 4, x + w - tx, s, TS_MED,
                  true);
  }
}

// --- the page -------------------------------------------------------------------
inline void draw(ToolsCanvas& c, const Info& in) {
  c.clear();
  char date[24];
  dateLine(in, date, sizeof(date));
  const bool land = c.width() > c.height();
  const int battY = c.height() - 44 - 22 - sectionHeight(c);
  if (!land) {
    const int L = 28, W = c.width() - 2 * L;
    c.textTracked(L, 30, date, LBL, true, true, 2);
    if (in.pic) {
      // The picture wants the height: the clock at three quarters.
      if (in.haveClock) bigClock(c, L, 62, in.hh, in.mm, 3, 4);
      weatherBeside(c, in, L, 62 + bigdigits::H * 3 / 4 + 18, W);
    } else {
      if (in.haveClock) bigClock(c, (c.width() - clockWidth()) / 2, 66, in.hh, in.mm);
      weather(c, in, L, 262, W, 104);
    }
    battery(c, in, L, battY, W);
  } else if (in.pic) {
    // The picture down the right at half size (240x400); clock, a weather
    // row and the battery on the left.
    const int PX = c.width() - 28 - 240, L = 36, LW = PX - 28 - L;
    picture(c, in, PX, 18, 1, 2);
    c.textTracked(L, 20, date, LBL, true, true, 2);
    if (in.haveClock) bigClock(c, L, 54, in.hh, in.mm);
    weatherRow(c, in, L, 54 + bigdigits::H + 12, LW);
    battery(c, in, L, battY, LW);
  } else {
    const int L = 36, LW = clockWidth();
    c.textTracked(L, 24, date, LBL, true, true, 2);
    if (in.haveClock) bigClock(c, L, 62, in.hh, in.mm);
    battery(c, in, L, battY, LW);
    const int RX = L + LW + 24;
    weather(c, in, RX, 30, c.width() - 24 - RX, 64, 3, 4);
  }
  footer(c);
}

// --- the popup (portrait only, the kit's system banner) ----------------------------
// Drawn over whatever is on the panel: the caller fades it first. Returns the
// Cancel button's rectangle.
inline TRect drawPopup(ToolsCanvas& c, int secs, int pct) {
  char label[24], title[32];
  if (pct >= 0) snprintf(label, sizeof(label), "CHARGING \xc2\xb7 %d%%", pct);
  else snprintf(label, sizeof(label), "CHARGING");
  snprintf(title, sizeof(title), "Dashboard in %d s", secs < 1 ? 1 : secs);
  const int okH = 56;
  const int bottom = chrome2::banner(c, c.height() / 2, chrome2::BI_BATTERY, label, title, nullptr,
                                     okH + 20, pct);
  const TRect r{28, bottom - 28 - okH, c.width() - 56, okH};
  c.fillRect(r.x, r.y, r.w, r.h, false);
  c.button(r.x, r.y, r.w, r.h, "Cancel", false);
  return r;
}

}  // namespace chgdash
