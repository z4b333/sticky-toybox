// Shared UI seam for the utility apps (coin / dice / timer / random / picker).
//
// The tool apps below are compiled UNCHANGED into two different firmwares:
//   * the standalone Toybox firmware (draws through Epd + gfx)
//   * the CrossPoint Reader port    (draws through GfxRenderer)
// Everything device-specific lives behind ToolsCanvas / ToolsHost, so a tool
// app never includes a display header. Both hosts are ESP32-Arduino, so
// Preferences, millis() and esp_random() are used directly.
#pragma once
#include <Arduino.h>
#include <Preferences.h>

#include "unicode.h"

// Text size buckets. Each host maps these onto whatever fonts it has.
enum TSize : uint8_t { TS_SMALL = 0, TS_MED = 1, TS_LARGE = 2, TS_HUGE = 3 };

// The smallest size a string is still readable at, given its script. Thai in a
// 16 px box is a 9 pt face -- the two mark storeys eat the line -- so anything
// below TS_LARGE is squint material; han and hangul hold up at 16 px on this
// 235 DPI panel but turn to mush in a 12 px line. Latin goes as small as the
// layout likes. Callers pass the size they want and draw what comes back.
inline TSize scriptFloor(const char* s, TSize sz) {
  if (sz >= TS_LARGE || !s) return sz;
  TSize floor = sz;
  for (const char* p = s; *p;) {
    const uint32_t cp = uni::next(p);
    if (uni::thai(cp)) return sz < TS_LARGE ? TS_LARGE : sz;
    if (uni::cjk(cp) && sz < TS_MED) floor = TS_MED;
  }
  return floor;
}

class ToolsCanvas {
 public:
  virtual ~ToolsCanvas() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;

  virtual void clear() = 0;
  virtual void fillRect(int x, int y, int w, int h, bool black) = 0;
  virtual void drawRect(int x, int y, int w, int h, int thickness, bool black) = 0;
  virtual void drawLine(int x0, int y0, int x1, int y1, int thickness, bool black) = 0;
  virtual void fillCircle(int cx, int cy, int r, bool black) = 0;
  virtual void drawCircle(int cx, int cy, int r, int thickness, bool black) = 0;

  virtual void text(int x, int y, const char* s, TSize sz, bool black, bool bold = false) = 0;
  virtual int textWidth(const char* s, TSize sz, bool bold = false) const = 0;
  virtual int textHeight(TSize sz) const = 0;

  // Blank space the glyphs leave inside textWidth/textHeight. A bitmap font
  // carries its advance as padding inside the cell -- usually all of it on one
  // side -- so centring on the reported box leaves the ink visibly off centre
  // at large sizes. A host that does not care can leave this at zero.
  virtual void textPad(const char* s, TSize sz, int& left, int& right, int& top,
                       int& bottom) const {
    (void)s;
    (void)sz;
    left = right = top = bottom = 0;
  }

  // --- convenience built on the primitives above ---------------------------
  void textCentered(int cx, int y, const char* s, TSize sz, bool black, bool bold = false) {
    int l, r, t, b;
    textPad(s, sz, l, r, t, b);
    text(cx - textWidth(s, sz, bold) / 2 + (r - l) / 2, y, s, sz, black, bold);
  }
  void textInBox(int x, int y, int w, int h, const char* s, TSize sz, bool black,
                 bool bold = false) {
    int l, r, t, b;
    textPad(s, sz, l, r, t, b);
    textCentered(x + w / 2, y + (h - textHeight(sz)) / 2 + (b - t) / 2, s, sz, black, bold);
  }
  // Filled = black background + white label (primary action).
  void button(int x, int y, int w, int h, const char* label, bool filled,
              TSize sz = TS_MED) {
    if (filled) {
      fillRect(x, y, w, h, true);
      textInBox(x, y, w, h, label, sz, false, true);
    } else {
      fillRect(x, y, w, h, false);
      drawRect(x, y, w, h, 2, true);
      textInBox(x, y, w, h, label, sz, true, false);
    }
  }
  // One glyph, for grids that place characters themselves.
  void textChar(int x, int y, char ch, TSize sz, bool black, bool bold = false) {
    const char s[2] = {ch, 0};
    text(x, y, s, sz, black, bold);
  }

  // Letter-spaced text, for the few titles that want air between the glyphs.
  // Built on text() one character at a time rather than on a spacing argument,
  // so a host whose text engine has no notion of tracking still gets it right.
  // Steps by codepoint, and a combining mark travels with its base -- a Thai
  // deck name in the top bar must not have its tone marks spaced away.
  int textTrackedWidth(const char* s, TSize sz, bool bold, int spacing) const {
    int w = 0;
    char one[16];
    for (const char* p = s; *p;) {
      w += textWidth(cluster(p, one), sz, bold) + spacing;
    }
    return w > 0 ? w - spacing : 0;
  }
  void textTracked(int x, int y, const char* s, TSize sz, bool black, bool bold, int spacing) {
    char one[16];
    for (const char* p = s; *p;) {
      const char* c = cluster(p, one);
      text(x, y, c, sz, black, bold);
      x += textWidth(c, sz, bold) + spacing;
    }
  }
  void textTrackedCentered(int cx, int y, const char* s, TSize sz, bool black, bool bold,
                           int spacing) {
    int l, r, t, b;
    textPad(s, sz, l, r, t, b);
    textTracked(cx - textTrackedWidth(s, sz, bold, spacing) / 2 + (r - l) / 2, y, s, sz, black,
                bold, spacing);
  }

  // Small square stepper button ("-" / "+").
  void stepper(int x, int y, int size, const char* label, bool enabled) {
    drawRect(x, y, size, size, enabled ? 2 : 1, true);
    textInBox(x, y, size, size, label, TS_LARGE, true, enabled);
  }

 private:
  // Copies the next base character plus any combining marks riding it into
  // `out`, advances p, and returns out. 16 bytes holds a base and three marks.
  static const char* cluster(const char*& p, char out[16]) {
    int n = 0;
    const char* start = p;
    uni::next(p);
    while (*p) {
      const char* q = p;
      if (!uni::thaiMark(uni::next(q))) break;
      p = q;
    }
    while (start < p && n < 15) out[n++] = *start++;
    out[n] = 0;
    return out;
  }
};

// Everything a tool app needs from the firmware it is embedded in.
class ToolsHost {
 public:
  virtual ~ToolsHost() = default;
  virtual ToolsCanvas& canvas() = 0;
  virtual Preferences& prefs() = 0;
  // Re-render the active tool and push it to the panel. full=true forces an
  // absolute refresh (slower, clears ghosting) — use on screen changes.
  virtual void refresh(bool full = false) = 0;
  virtual void beep(uint8_t kind) = 0;  // 0 tap, 1 confirm, 2 reject, 3 alarm
  virtual void goHub() = 0;
  virtual void topBar(const char* title, bool withHelp = false) = 0;
  // A tool with no rules card leaves this false and no "?" is drawn.
  virtual bool isHelpTap(int x, int y) const { return false; }
  virtual bool isBackTap(int x, int y) const = 0;
  // Is there anything above Toybox to go back to? The standalone firmware is
  // the whole device and answers no, so its hub is the root. Inside the reader
  // the hub is one activity deep, and the way out belongs in the same corner
  // every app already puts its back button.
  virtual bool canExit() const { return false; }
  virtual void exit() {}

  // Whether the device beeps at all. It belongs to the host: the reader has its
  // own idea about sound, and the standalone firmware keeps it in its own NVS.
  virtual bool soundOn() const { return true; }
  virtual void setSoundOn(bool on) { (void)on; }
  // Y coordinate where a tool's own content may start (below the top bar).
  virtual int contentTop() const = 0;
};

class ToolApp {
 public:
  virtual ~ToolApp() = default;
  virtual const char* title() const = 0;
  // Called every time the tool is opened from the hub.
  virtual void enter(ToolsHost& h) { _host = &h; }
  virtual void render(ToolsCanvas& c) = 0;
  virtual void onTap(int x, int y) = 0;
  virtual void onSwipe(int dx, int dy) { (void)dx; (void)dy; }
  virtual void tick() {}                       // ~50 Hz, for running timers
  virtual bool wantsTick() const { return false; }

 protected:
  ToolsHost* _host = nullptr;
  ToolsHost& host() { return *_host; }
  ToolsCanvas& canvas() { return _host->canvas(); }
  Preferences& prefs() { return _host->prefs(); }
};

// Rect hit-test shared by every tool.
inline bool tHit(int px, int py, int x, int y, int w, int h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}

struct TRect {
  int x, y, w, h;
  bool hit(int px, int py) const { return tHit(px, py, x, y, w, h); }
};
