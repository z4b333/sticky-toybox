// Minimal SSD1677 e-paper driver for the Seeed reTerminal Sticky (800x480 B/W).
// Waveform sequences follow the vendor demo / FreeInk SDK Sticky config:
//   FULL:    border 0x01, CTRL2=0xF7  (~1.7 s, absolute, clears ghosting)
//   PARTIAL: border 0x80, CTRL2=0xFF  (fast DU, differential vs previous frame)
// Framebuffer: 1bpp, MSB-first within a byte, 1 = white, 0 = black.
#pragma once
#include <Arduino.h>

#include "board_pins.h"

// Where a logical pixel lands on the panel. This lives in the header, and both
// the driver and the preview harness call it, because they used to each keep a
// copy and the copies drifted twice: once losing the board flips entirely, and
// once keeping the two landscape rotations transposed after the driver was
// fixed. A guard cannot catch a mistake in code it is not running.
//
// Rotation 0 is the quarter turn every layout assumes: logical (x, y) ->
// panel (PANEL_W-1-y, x). The other three compose a flip or identity on top.
// 1 means the device has been turned a quarter turn anticlockwise, so the image
// turns a quarter clockwise to stay upright -- which puts the content's up
// direction along the device's right-hand side. 3 is the mirror of that. The
// board correction is applied last, in panel space, so nothing above it has to
// know which way the panel was wired.
inline void epdMapPixel(int rot, bool flipX, bool flipY, int x, int y, int& px, int& py) {
  switch (rot & 3) {
    case 1: px = PANEL_W - 1 - x; py = PANEL_H - 1 - y; break;  // quarter turn cw
    case 2: px = y; py = PANEL_H - 1 - x; break;                // portrait, flipped
    case 3: px = x; py = y; break;                              // quarter turn ccw
    default: px = PANEL_W - 1 - y; py = x; break;               // portrait
  }
  if (flipX) px = PANEL_W - 1 - px;
  if (flipY) py = PANEL_H - 1 - py;
}

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

  // Did the panel answer at boot?
  //
  // begin() returns true if the framebuffer allocated, which says nothing at
  // all about whether a display is listening. A Sticky whose panel ignores SPI
  // boots, logs, sleeps and beeps perfectly while showing whatever image was on
  // the glass before -- and e-paper holds that image for ever, so the owner
  // sees the firmware they flashed over. Somebody spent an evening on that.
  //
  // BUSY is the one line the panel drives, so it is the only thing that can be
  // asked. This is advisory: nothing behaves differently, it is reported on the
  // serial log, on the service screen, and on the buzzer, because the one thing
  // you cannot use to report a broken display is the display.
  bool panelAnswered() const { return _panelOk; }

  // Reset the controller and ask again, without touching the framebuffer.
  // Used after the SD card has had the bus, which is the one situation where a
  // panel that was answering a moment ago might not be.
  void reinit();
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

  // Four-level grey, for the book reader's 2-bit pages. Takes a packed 2bpp
  // portrait page (480x800, four pixels a byte, high bits first, 0 = black
  // ... 3 = white) and runs the whole multi-pass sequence: an absolute B/W
  // pass with the greys black, then a custom-LUT pass that lifts them to
  // their levels, then a baseline resync so the next differential update has
  // something honest to diff against. Borrows both internal buffers, so the
  // partial-refresh shadow is gone afterwards; _firstPaint is re-armed and
  // the next UI paint promotes itself to full. The custom waveform and its
  // Sticky voltage tail come from the CrossPoint reader project (MIT) -- see
  // THIRD-PARTY.md; the factory waveform is restored automatically because
  // the next full refresh reloads the OTP LUT.
  // Where a grey page's 2-bit pixels come from.
  //
  // The plane builder reads the page three times and strictly in order, so a
  // 96,000-byte page never has to be in memory at once -- and on this device
  // it must not be, because 96 KB of contiguous heap is not reliably there
  // once the UI has been running. A reader hands over the card instead and
  // the page streams through a band at a time.
  struct GreySource {
    virtual ~GreySource() = default;
    // n bytes from byte `off` of the page. False on a short read: a plane
    // built from half a page is worse than no page at all.
    virtual bool read(uint32_t off, uint8_t* dst, uint32_t n) = 0;
  };

  bool displayGrey2bpp(const uint8_t* packed2bpp);   // the whole page in RAM
  bool displayGrey2bpp(GreySource& src);             // ...or streamed
  // Partial refresh: fast differential update against the previous frame.
  // Automatically promotes to full every FULL_EVERY partials (ghosting control),
  // and on the first paint after boot.
  void displayPartial();

  void deepSleep();  // panel deep sleep (RAM discarded)

  int partialCount() const { return _partialsSinceFull; }

  static constexpr int FULL_EVERY = 40;

 private:
  int _rot = 0;
  bool _panelOk = false;
  bool _flipX = false, _flipY = false;

 public:

 private:
  void buildGreyPlane(GreySource& src, uint8_t* dst, int which);
  void writeCmd(uint8_t c);
  void writeData(uint8_t d);
  void writeData(const uint8_t* d, uint32_t len);
  void waitBusy(uint32_t timeoutMs = 5000);
  void reset();
  bool probePanel();
  void initController();
  void setRamAreaFull();

  uint8_t* _fb = nullptr;    // current frame
  uint8_t* _prev = nullptr;  // previous frame (differential baseline)
  bool _firstPaint = true;
  int _partialsSinceFull = 0;
};

extern Epd epd;
