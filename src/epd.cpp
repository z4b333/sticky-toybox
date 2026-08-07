#include "epd.h"

#include <SPI.h>

#include "board_pins.h"

Epd epd;

// SSD1677 commands
namespace {
constexpr uint8_t CMD_SOFT_RESET = 0x12;
constexpr uint8_t CMD_TEMP_SENSOR = 0x18;
constexpr uint8_t CMD_BOOSTER = 0x0C;
constexpr uint8_t CMD_OUTPUT_CTRL = 0x01;
constexpr uint8_t CMD_BORDER = 0x3C;
constexpr uint8_t CMD_DATA_ENTRY = 0x11;
constexpr uint8_t CMD_RAM_X_RANGE = 0x44;
constexpr uint8_t CMD_RAM_Y_RANGE = 0x45;
constexpr uint8_t CMD_RAM_X_CNT = 0x4E;
constexpr uint8_t CMD_RAM_Y_CNT = 0x4F;
constexpr uint8_t CMD_WRITE_BW = 0x24;
constexpr uint8_t CMD_WRITE_RED = 0x26;  // holds the "old" frame for DU diff
constexpr uint8_t CMD_UPDATE_CTRL1 = 0x21;
constexpr uint8_t CMD_UPDATE_CTRL2 = 0x22;
constexpr uint8_t CMD_ACTIVATE = 0x20;
constexpr uint8_t CMD_DEEP_SLEEP = 0x10;

constexpr uint8_t CTRL1_NORMAL = 0x00;      // partial: diff BW vs RED
constexpr uint8_t CTRL1_BYPASS_RED = 0x40;  // full: absolute, ignore RED

// Vendor sequences (power-cycle the panel internally each refresh)
constexpr uint8_t SEQ_FULL = 0xF7;
constexpr uint8_t SEQ_PARTIAL = 0xFF;
constexpr uint8_t BORDER_INIT = 0x01;
constexpr uint8_t BORDER_FULL = 0x01;
constexpr uint8_t BORDER_PARTIAL = 0x80;

const SPISettings kSpi(10000000, MSBFIRST, SPI_MODE0);  // conservative 10 MHz
}  // namespace

bool Epd::begin() {
  _fb = (uint8_t*)malloc(EPD_BUF_SIZE);
  _prev = (uint8_t*)malloc(EPD_BUF_SIZE);
  if (!_fb || !_prev) return false;
  memset(_fb, 0xFF, EPD_BUF_SIZE);
  memset(_prev, 0xFF, EPD_BUF_SIZE);

  // Panel power rail first, then SPI, then reset pulse.
  pinMode(PIN_EPD_PWR_EN, OUTPUT);
  digitalWrite(PIN_EPD_PWR_EN, HIGH);
  delay(100);

  SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_BUSY, INPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, HIGH);

  reset();
  initController();
  _firstPaint = true;
  return true;
}

void Epd::reset() {
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
  digitalWrite(PIN_EPD_RST, LOW);
  delay(2);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
}

void Epd::writeCmd(uint8_t c) {
  SPI.beginTransaction(kSpi);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(c);
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();
}

void Epd::writeData(uint8_t d) {
  SPI.beginTransaction(kSpi);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(d);
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();
}

void Epd::writeData(const uint8_t* d, uint32_t len) {
  SPI.beginTransaction(kSpi);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.writeBytes(d, len);
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();
}

void Epd::waitBusy(uint32_t timeoutMs) {
  const uint32_t start = millis();
  // BUSY is active HIGH on the SSD1677
  while (digitalRead(PIN_EPD_BUSY) == HIGH) {
    delay(1);
    if (millis() - start > timeoutMs) break;
  }
}

void Epd::initController() {
  writeCmd(CMD_SOFT_RESET);
  waitBusy();

  writeCmd(CMD_TEMP_SENSOR);
  writeData(0x80);  // internal sensor

  writeCmd(CMD_BOOSTER);
  const uint8_t booster[5] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
  writeData(booster, 5);

  writeCmd(CMD_OUTPUT_CTRL);
  writeData((EPD_H - 1) % 256);
  writeData((EPD_H - 1) / 256);
  writeData(0x02);  // scan config (SM interlaced), no TB flip

  writeCmd(CMD_BORDER);
  writeData(BORDER_INIT);

  setRamAreaFull();
}

// Gates are physically reversed on this panel: address Y from the far end,
// data entry X-increment / Y-decrement (matches the vendor demo & FreeInk SDK).
void Epd::setRamAreaFull() {
  writeCmd(CMD_DATA_ENTRY);
  writeData(0x01);  // X inc, Y dec

  writeCmd(CMD_RAM_X_RANGE);
  writeData(0x00);
  writeData(0x00);
  writeData((EPD_W - 1) % 256);
  writeData((EPD_W - 1) / 256);

  writeCmd(CMD_RAM_Y_RANGE);
  writeData((EPD_H - 1) % 256);
  writeData((EPD_H - 1) / 256);
  writeData(0x00);
  writeData(0x00);

  writeCmd(CMD_RAM_X_CNT);
  writeData(0x00);
  writeData(0x00);

  writeCmd(CMD_RAM_Y_CNT);
  writeData((EPD_H - 1) % 256);
  writeData((EPD_H - 1) / 256);
}

void Epd::clear(bool white) { memset(_fb, white ? 0xFF : 0x00, EPD_BUF_SIZE); }

// Logical -> panel mapping. Rotating here rather than in the drawing code means
// every primitive AND every glyph comes out rotated for free: the 8x8 font is
// blitted pixel by pixel through this function, so its characters turn with
// everything else and no separate rotated text path is needed.
void Epd::drawPixel(int x, int y, uint8_t color) {
  if (x < 0 || y < 0 || x >= EPD_W || y >= EPD_H) return;
#ifdef TOYBOX_PORTRAIT
  // Quarter turn: logical (x, y) -> panel (PANEL_W-1-y, x).
  const int px = PANEL_W - 1 - y;
  const int py = x;
#else
  const int px = x;
  const int py = y;
#endif
  uint8_t* p = &_fb[(uint32_t)py * EPD_WB + (px >> 3)];
  const uint8_t mask = 0x80 >> (px & 7);
  if (color)
    *p |= mask;
  else
    *p &= ~mask;
}

void Epd::fillRect(int x, int y, int w, int h, uint8_t color) {
  if (w <= 0 || h <= 0) return;
  const int x1 = max(0, x), y1 = max(0, y);
  const int x2 = min(EPD_W, x + w), y2 = min(EPD_H, y + h);
#ifdef TOYBOX_PORTRAIT
  for (int yy = y1; yy < y2; yy++)
    for (int xx = x1; xx < x2; xx++) drawPixel(xx, yy, color);
#else
  for (int yy = y1; yy < y2; yy++) {
    uint8_t* row = &_fb[(uint32_t)yy * EPD_WB];
    for (int xx = x1; xx < x2; xx++) {
      const uint8_t mask = 0x80 >> (xx & 7);
      if (color)
        row[xx >> 3] |= mask;
      else
        row[xx >> 3] &= ~mask;
    }
  }
#endif
}

void Epd::drawRect(int x, int y, int w, int h, uint8_t color, int thickness) {
  fillRect(x, y, w, thickness, color);
  fillRect(x, y + h - thickness, w, thickness, color);
  fillRect(x, y, thickness, h, color);
  fillRect(x + w - thickness, y, thickness, h, color);
}

// Bresenham with a thickness band. Only ever used for axis-aligned or 45-degree
// strokes here, so widening along the minor axis is enough.
void Epd::drawLine(int x0, int y0, int x1, int y1, uint8_t color, int thickness) {
  const int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  const bool steep = -dy > dx;  // widen across the shallower axis
  int err = dx + dy;
  if (thickness < 1) thickness = 1;
  const int half = thickness / 2;
  for (;;) {
    if (steep)
      fillRect(x0 - half, y0, thickness, 1, color);
    else
      fillRect(x0, y0 - half, 1, thickness, color);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void Epd::fillCircle(int cx, int cy, int r, uint8_t color) {
  if (r <= 0) return;
  for (int dy = -r; dy <= r; dy++) {
    const int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
  }
}

void Epd::drawCircle(int cx, int cy, int r, uint8_t color, int thickness) {
  if (r <= 0) return;
  if (thickness < 1) thickness = 1;
  const int inner = r - thickness;
  for (int dy = -r; dy <= r; dy++) {
    const int outDx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    if (inner > 0 && abs(dy) < inner) {
      const int inDx = (int)(sqrtf((float)(inner * inner - dy * dy)) + 0.5f);
      fillRect(cx - outDx, cy + dy, outDx - inDx + 1, 1, color);
      fillRect(cx + inDx, cy + dy, outDx - inDx + 1, 1, color);
    } else {
      fillRect(cx - outDx, cy + dy, 2 * outDx + 1, 1, color);
    }
  }
}

void Epd::displayFull() {
  setRamAreaFull();
  writeCmd(CMD_UPDATE_CTRL1);
  writeData(CTRL1_BYPASS_RED);
  writeCmd(CMD_BORDER);
  writeData(BORDER_FULL);

  writeCmd(CMD_WRITE_BW);
  writeData(_fb, EPD_BUF_SIZE);
  writeCmd(CMD_WRITE_RED);
  writeData(_fb, EPD_BUF_SIZE);

  writeCmd(CMD_UPDATE_CTRL2);
  writeData(SEQ_FULL);
  writeCmd(CMD_ACTIVATE);
  waitBusy(8000);

  memcpy(_prev, _fb, EPD_BUF_SIZE);
  _firstPaint = false;
  _partialsSinceFull = 0;
}

void Epd::displayPartial() {
  if (_firstPaint || _partialsSinceFull >= FULL_EVERY) {
    displayFull();
    return;
  }
  setRamAreaFull();
  writeCmd(CMD_UPDATE_CTRL1);
  writeData(CTRL1_NORMAL);
  writeCmd(CMD_BORDER);
  writeData(BORDER_PARTIAL);

  writeCmd(CMD_WRITE_BW);
  writeData(_fb, EPD_BUF_SIZE);
  writeCmd(CMD_WRITE_RED);
  writeData(_prev, EPD_BUF_SIZE);

  writeCmd(CMD_UPDATE_CTRL2);
  writeData(SEQ_PARTIAL);
  writeCmd(CMD_ACTIVATE);
  waitBusy(5000);

  memcpy(_prev, _fb, EPD_BUF_SIZE);
  _partialsSinceFull++;
}

void Epd::deepSleep() {
  writeCmd(CMD_DEEP_SLEEP);
  writeData(0x03);
}
