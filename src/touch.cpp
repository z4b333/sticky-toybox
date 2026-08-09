#include "touch.h"

#include <Wire.h>
#include <driver/gpio.h>

#include "board_pins.h"

Touch touch;

bool Touch::begin() {
  // Touch power rail first (active high), settle, then reset dance + probe.
  gpio_hold_dis((gpio_num_t)PIN_TP_EN);
  pinMode(PIN_TP_EN, OUTPUT);
  digitalWrite(PIN_TP_EN, HIGH);
  delay(50);

  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  Wire.setTimeOut(10);

  _addr = 0;
  resetWithIntLevel(LOW);
  if (!probe()) {
    resetWithIntLevel(HIGH);
    probe();
  }
  return _addr != 0;
}

void Touch::resetWithIntLevel(uint8_t level) {
  pinMode(PIN_TP_INT, OUTPUT);
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  digitalWrite(PIN_TP_INT, level);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(10);
  digitalWrite(PIN_TP_INT, level);
  delay(50);
  pinMode(PIN_TP_INT, INPUT);
  delay(50);
}

bool Touch::probe() {
  const uint8_t candidates[2] = {GT911_ADDR, GT911_ADDR_ALT};
  for (uint8_t a : candidates) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      _addr = a;
      return true;
    }
  }
  return false;
}

bool Touch::readReg(uint16_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(_addr, len, (uint8_t)1) != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void Touch::clearStatus() {
  Wire.beginTransmission(_addr);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();
}

void Touch::poll(TouchEvent& ev) {
  ev = TouchEvent{};
  if (_addr == 0) return;

  uint8_t status = 0;
  if (!readReg(0x814E, &status, 1)) return;
  if (!(status & 0x80)) return;  // no fresh frame

  const uint8_t count = status & 0x0F;
  if (count > 0) {
    uint8_t pt[8] = {};
    if (readReg(0x8150, pt, 8)) {
      // coords at byte 0 (Sticky quirk)
      const uint16_t rawX = pt[0] | (pt[1] << 8);
      const uint16_t rawY = pt[2] | (pt[3] << 8);
      // Swap axes (portrait digitizer), then flip to panel-native landscape.
      // All three are adjustable from the service screen: they were read off
      // vendor bring-up notes, not measured on a board.
      int px = _swap ? rawY : rawX;
      int py = _swap ? rawX : rawY;
      px = constrain(px, 0, PANEL_W - 1);
      py = constrain(py, 0, PANEL_H - 1);
      if (_tfx) px = (PANEL_W - 1) - px;
      if (_tfy) py = (PANEL_H - 1) - py;
      // Undo whatever correction the panel needed, so once the image is the
      // right way round the taps follow it without a second adjustment.
      if (_pfx) px = PANEL_W - 1 - px;
      if (_pfy) py = PANEL_H - 1 - py;
      // Inverse of the display transform in epd.cpp, so a tap lands on the
      // control the user actually sees. Kept case-for-case with drawPixel:
      // if one changes, the other has to.
      int x, y;
      switch (_rot) {
        case 1: x = PANEL_W - 1 - px; y = PANEL_H - 1 - py; break;
        case 2: x = PANEL_H - 1 - py; y = px; break;
        case 3: x = px; y = py; break;
        default: x = py; y = PANEL_W - 1 - px; break;
      }
      if (!_down) {
        _down = true;
        _downX = x;
        _downY = y;
      }
      _lastX = x;
      _lastY = y;
    }
  } else if (_down) {
    _down = false;
    const int dx = _lastX - _downX;
    const int dy = _lastY - _downY;
    if (abs(dx) <= TAP_SLOP && abs(dy) <= TAP_SLOP) {
      ev.tapped = true;
      ev.x = _downX;
      ev.y = _downY;
    } else if (abs(dx) >= SWIPE_MIN || abs(dy) >= SWIPE_MIN) {
      ev.swiped = true;
      ev.dx = dx;
      ev.dy = dy;
      ev.x = _downX;
      ev.y = _downY;
    }
  }
  clearStatus();  // GT911 requires clearing 0x814E after each read
}
