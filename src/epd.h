// Minimal SSD1677 e-paper driver for the Seeed reTerminal Sticky (800x480 B/W).
// Waveform sequences follow the vendor demo / FreeInk SDK Sticky config:
//   FULL:    border 0x01, CTRL2=0xF7  (~1.7 s, absolute, clears ghosting)
//   PARTIAL: border 0x80, CTRL2=0xFF  (fast DU, differential vs previous frame)
// Framebuffer: 1bpp, MSB-first within a byte, 1 = white, 0 = black.
#pragma once
#include <Arduino.h>

#include "board_pins.h"

class Epd {
 public:
  // Allocates the 48 KB framebuffer + 48 KB shadow (previous frame).
  bool begin();

  // Drawing state
  uint8_t* fb() { return _fb; }
  void clear(bool white = true);

  // Runtime rotation of the logical coordinate space, for the pinned note
  // following the accelerometer. 0 = portrait (the default every app assumes),
  // 1/3 = the two landscapes, 2 = portrait upside down. Logical width and
  // height swap for the landscape pair; everything drawn through drawPixel
  // rotates with it.
  void setRotation(int r) { _rot = r & 3; }

  // Board correction, set once at boot from what the service screen saved.
  // The SSD1677's scan-direction bits are a guess taken from vendor demo code;
  // if they are wrong for a board revision the image comes out mirrored or
  // upside down, and these two flips cover every way that can happen. Applied
  // last, in panel space, so nothing above them has to know.
  void setPanelFlip(bool fx, bool fy) { _flipX = fx; _flipY = fy; }
  bool panelFlipX() const { return _flipX; }
  bool panelFlipY() const { return _flipY; }
  int rotation() const { return _rot; }
  int logicalW() const { return (_rot & 1) ? PANEL_W : PANEL_H; }
  int logicalH() const { return (_rot & 1) ? PANEL_H : PANEL_W; }

  // Pixel helpers (x: 0..799, y: 0..479); color: 0 = black, 1 = white
  void drawPixel(int x, int y, uint8_t color);
  void fillRect(int x, int y, int w, int h, uint8_t color);
  void drawRect(int x, int y, int w, int h, uint8_t color, int thickness = 1);
  void drawHLine(int x, int y, int w, uint8_t color) { fillRect(x, y, w, 1, color); }
  void drawVLine(int x, int y, int h, uint8_t color) { fillRect(x, y, 1, h, color); }
  void drawLine(int x0, int y0, int x1, int y1, uint8_t color, int thickness = 1);
  void fillCircle(int cx, int cy, int r, uint8_t color);
  void drawCircle(int cx, int cy, int r, uint8_t color, int thickness = 1);

  // Push the framebuffer to the panel.
  // Full refresh: absolute waveform, clears ghosting; use on screen changes.
  void displayFull();
  // Partial refresh: fast differential update against the previous frame.
  // Automatically promotes to full every FULL_EVERY partials (ghosting control),
  // and on the first paint after boot.
  void displayPartial();

  void deepSleep();  // panel deep sleep (RAM discarded)

  int partialCount() const { return _partialsSinceFull; }

  static constexpr int FULL_EVERY = 40;

 private:
  int _rot = 0;
  bool _flipX = false, _flipY = false;

 public:

 private:
  void writeCmd(uint8_t c);
  void writeData(uint8_t d);
  void writeData(const uint8_t* d, uint32_t len);
  void waitBusy(uint32_t timeoutMs = 5000);
  void reset();
  void initController();
  void setRamAreaFull();

  uint8_t* _fb = nullptr;    // current frame
  uint8_t* _prev = nullptr;  // previous frame (differential baseline)
  bool _firstPaint = true;
  int _partialsSinceFull = 0;
};

extern Epd epd;
