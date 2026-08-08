// GT911 capacitive touch for the reTerminal Sticky.
// Quirks handled (per FreeInk SDK bring-up notes):
//   * coords start at byte 0 of the point record (no track-id byte)
//   * portrait digitizer on landscape panel: swap X/Y, then flip both axes
//   * TOUCH_EN rail (GPIO42) must be high before the reset/probe dance
#pragma once
#include <Arduino.h>

struct TouchEvent {
  bool tapped = false;    // finger lifted without moving beyond slop
  bool swiped = false;    // finger lifted after moving beyond swipe threshold
  int x = 0, y = 0;       // tap position (panel coords, 800x480 landscape)
  int dx = 0, dy = 0;     // swipe delta (up/down/left/right from sign & magnitude)
};

class Touch {
 public:
  bool begin();
  // Poll; fills ev when a gesture completed this cycle. Call frequently.
  void poll(TouchEvent& ev);
  bool ok() const { return _addr != 0; }
  uint8_t address() const { return _addr; }
  // Must track Epd::setRotation, or a tap lands where the pixel used to be.
  // Only the pinned note rotates; every app screen stays at 0.
  void setRotation(int r) { _rot = r & 3; }

  // Board corrections, set once at boot from what the service screen saved.
  // The digitizer's own orientation (swap, then two flips) is the guess from
  // the vendor notes; the panel pair is whatever correction the display needed,
  // undone here so a tap still lands on the pixel the user is looking at.
  void setMapping(bool swapXY, bool flipX, bool flipY) {
    _swap = swapXY;
    _tfx = flipX;
    _tfy = flipY;
  }
  void setPanelFlip(bool fx, bool fy) { _pfx = fx; _pfy = fy; }

 private:
  bool readReg(uint16_t reg, uint8_t* buf, uint8_t len);
  void clearStatus();
  void resetWithIntLevel(uint8_t level);
  bool probe();

  uint8_t _addr = 0;
  int _rot = 0;
  bool _swap = true, _tfx = true, _tfy = true;
  bool _pfx = false, _pfy = false;
  bool _down = false;
  int _downX = 0, _downY = 0;
  int _lastX = 0, _lastY = 0;
  static constexpr int TAP_SLOP = 14;
  static constexpr int SWIPE_MIN = 40;
};

extern Touch touch;
