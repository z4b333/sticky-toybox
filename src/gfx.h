// Text + widget drawing on top of the Epd framebuffer.
#pragma once
#include <Arduino.h>

#include "epd.h"

namespace gfx {

// Draw a single char at (x, y), scale = pixel multiplier. Color 0=black 1=white.
void drawChar(int x, int y, char c, int scale, uint8_t color);

// Draw a string; returns width in pixels. spacing: extra px between chars.
int drawText(int x, int y, const char* s, int scale, uint8_t color, bool bold = false,
             int spacing = 0);
int textWidth(const char* s, int scale, bool bold = false, int spacing = 0);

// The typeface the Latin tables answer with: 0 = DejaVu (UI and default),
// 1 = Literata, 2 = Atkinson Hyperlegible. The EPUB reader sets this around
// its own text; everything else leaves it alone.
void setTypeface(int n);
int typeface();
int textHeight(int scale);

// Blank space left inside that width and height by the glyphs themselves, so
// callers can centre on the ink rather than on the box.
void textInk(const char* s, int scale, bool bold, int spacing, int& left, int& right,
             int& top, int& bottom);

// Centered helpers
void drawTextCentered(int cx, int y, const char* s, int scale, uint8_t color,
                      bool bold = false, int spacing = 0);

// A tappable rounded-ish button: filled or outlined, with centered label.
void drawButton(int x, int y, int w, int h, const char* label, int scale, bool filled);

// Loads every *.tfp font pack under /fonts into PSRAM and registers its faces
// behind the baked tables (full Chinese / Korean / Japanese coverage; see
// tools/make_font_pack.py). Safe to call again after an install; packs already
// loaded are kept. Returns how many faces are live.
int loadFontPacks();
// How many faces the last call found, for the service screen to report without
// mapping the partitions a second time.
int loadedFaceCount();

#ifdef TOYBOX_HOST
// Host-only text-overflow log, drained by the preview harness.
struct Overflow {
  char text[64];
  int x, width, scale;
};
extern Overflow g_overflow[64];
extern int g_overflowCount;
extern const char* g_overflowScreen;
extern char g_overflowScreens[64][40];
#endif

}  // namespace gfx
