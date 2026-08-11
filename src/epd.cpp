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

  // Deselect the SD card before anything touches this bus.
  //
  // The card shares SCK, MOSI and MISO with the panel, and its chip select is
  // GPIO 8 -- which, until this line existed, was never configured at all on a
  // normal boot. A floating CS means the card can consider itself selected
  // while the display is being written to, and then two devices are driving one
  // bus. With a card in the slot that produced a screen full of glitches and a
  // card that could not be found afterwards, because it had already been
  // clocked into a state it does not answer from.
  //
  // First, before SPI.begin and before the panel rail comes up. The rule on a
  // shared bus is that every device is deselected before any of them is spoken
  // to, and there is no moment early enough to make an exception for.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

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
  _panelOk = probePanel();
  initController();
  _firstPaint = true;
  return true;
}

// Ask the panel whether it is there, using the only line it drives.
//
// A soft reset makes the SSD1677 hold BUSY high for a few milliseconds and then
// release it. Nothing connected leaves the line wherever the pull resistor puts
// it, and it never moves. So: send the reset, watch for a rise, then watch for
// the fall. Either half failing means no panel answered.
//
// The rise window is generous at 100 ms and the fall window at five seconds.
// Both are far longer than the datasheet needs, because the cost of being too
// impatient here is telling somebody with a perfectly good display that theirs
// is broken, which is worse than saying nothing.
bool Epd::probePanel() {
  writeCmd(CMD_SOFT_RESET);

  const uint32_t t0 = millis();
  bool rose = false;
  while (millis() - t0 < 100) {
    if (digitalRead(PIN_EPD_BUSY) == HIGH) {
      rose = true;
      break;
    }
    delay(1);
  }
  if (!rose) return false;

  const uint32_t t1 = millis();
  while (digitalRead(PIN_EPD_BUSY) == HIGH) {
    if (millis() - t1 > 5000) return false;  // held busy for ever: wedged
    delay(1);
  }
  return true;
}

void Epd::reinit() {
  reset();
  _panelOk = probePanel();
  initController();
  _firstPaint = true;  // the controller's RAM is gone; the next paint must be full
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

  // The controller is told about the panel, never about the logical canvas.
  // EPD_W/EPD_H are the portrait space the apps draw in (480x800); the panel
  // and the framebuffer are both 800x480, and drawPixel is what bridges them.
  // Handing the portrait numbers to the controller made a row 60 bytes wide
  // where the framebuffer supplies 100, so every row spilled into the next and
  // the image repeated down the panel, while the 320 columns never declared
  // showed whatever was already in RAM.
  writeCmd(CMD_OUTPUT_CTRL);
  writeData((PANEL_H - 1) % 256);
  writeData((PANEL_H - 1) / 256);
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
  writeData((PANEL_W - 1) % 256);
  writeData((PANEL_W - 1) / 256);

  writeCmd(CMD_RAM_Y_RANGE);
  writeData((PANEL_H - 1) % 256);
  writeData((PANEL_H - 1) / 256);
  writeData(0x00);
  writeData(0x00);

  writeCmd(CMD_RAM_X_CNT);
  writeData(0x00);
  writeData(0x00);

  writeCmd(CMD_RAM_Y_CNT);
  writeData((PANEL_H - 1) % 256);
  writeData((PANEL_H - 1) / 256);
}

void Epd::clear(bool white) { memset(_fb, white ? 0xFF : 0x00, EPD_BUF_SIZE); }

// Logical -> panel mapping. Rotating here rather than in the drawing code means
// every primitive AND every glyph comes out rotated for free: the 8x8 font is
// blitted pixel by pixel through this function, so its characters turn with
// everything else and no separate rotated text path is needed.
void Epd::drawPixel(int x, int y, uint8_t color) {
  if (x < 0 || y < 0 || x >= logicalW() || y >= logicalH()) return;
  int px, py;
  epdMapPixel(_rot, _flipX, _flipY, x, y, px, py);
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
  const int x2 = min(logicalW(), x + w), y2 = min(logicalH(), y + h);
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

// --- four-level grey ----------------------------------------------------------
// The waveform below is the CrossPoint reader project's Sticky grayscale LUT
// (MIT, see THIRD-PARTY.md), carried verbatim: 50 bytes of VS rows (five rows
// of ten), 50 of TP/RP timing, 5 of frame rate, then the voltage tail
// VGH/VSH1/VSH2/VSL/VCOM and two reserved bytes. The voltage tail is
// per-module analog calibration -- their tuning notes say: if the mid-greys
// sit wrong, move VCOM (byte 109) in single steps first, then VSH1 (106).
//
// Row semantics, keyed by the (RED, BW) RAM bit pair per pixel:
//   00 -> no drive: the pixel keeps whatever the B/W pass left (black or white)
//   10 -> "gray"       (the lighter of the two mids)
//   11 -> "dark gray"
// The 01 row exists in the table but this mapping never selects it, matching
// the papyrix/CrossPoint renderers byte for byte.
namespace {
constexpr uint8_t GREY_LUT[112] = {
    // VS rows: 00 keep, 01 light (unused), 10 gray, 11 dark gray, VCOM
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // TP/RP timing
    0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,
    // voltages: VGH, VSH1, VSH2, VSL, VCOM -- the Sticky tuning point
    0x17, 0x41, 0xA8, 0x32, 0x30,
    // reserved
    0x00, 0x00};

constexpr uint8_t CMD_WRITE_LUT = 0x32;
constexpr uint8_t CMD_GATE_V = 0x03;
constexpr uint8_t CMD_SOURCE_V = 0x04;
constexpr uint8_t CMD_VCOM = 0x2C;
// External-LUT activation: clock/analog on + display + power down, WITHOUT the
// OTP LUT reload bit (0x10) that 0xF7/0xFF carry -- that bit would overwrite
// the table just loaded.
constexpr uint8_t SEQ_GREY = 0xCF;
constexpr uint8_t BORDER_GREY = 0x80;  // park at VCOM: a follow-LUT border
                                       // would paint the frame black
}  // namespace

// Which plane a pixel level lights, per the row semantics above.
//   BW pass: white only for level 3 (greys start black and get lifted).
//   RED (msb): set for both mids.  BW-plane (lsb): set for the dark mid only.
void Epd::buildGreyPlane(const uint8_t* packed, uint8_t* dst, int which) {
  memset(dst, 0x00, EPD_BUF_SIZE);
  for (int y = 0; y < EPD_H; y++) {
    for (int x = 0; x < EPD_W; x++) {
      const uint32_t i = (uint32_t)y * EPD_W + x;
      const uint8_t lv = (packed[i >> 2] >> (6 - 2 * (i & 3))) & 3;
      bool bit;
      switch (which) {
        case 0: bit = (lv == 3); break;             // the B/W pass: white
        case 1: bit = (lv == 1); break;             // BW plane: dark grey
        default: bit = (lv == 1 || lv == 2); break; // RED plane: both mids
      }
      if (!bit) continue;
      int px, py;
      epdMapPixel(0, _flipX, _flipY, x, y, px, py);
      dst[(uint32_t)py * EPD_WB + (px >> 3)] |= (0x80 >> (px & 7));
    }
  }
}

bool Epd::displayGrey2bpp(const uint8_t* packed) {
  if (!_fb || !_prev || !packed) return false;

  // Pass 1: absolute black-and-white, greys rendered black. SEQ_FULL reloads
  // the factory OTP waveform as a side effect, which is exactly the state the
  // custom LUT wants to be loaded over.
  buildGreyPlane(packed, _fb, 0);
  // displayFull() maps nothing -- _fb is already in panel space -- so drive
  // the sequence directly rather than letting it re-copy the shadow.
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

  // Keep the pass-1 frame: it is the baseline both RAMs get afterwards.
  memcpy(_prev, _fb, EPD_BUF_SIZE);

  // Load the custom LUT: 105 waveform bytes, then the voltage tail into its
  // three registers, then the border parked at VCOM.
  writeCmd(CMD_WRITE_LUT);
  writeData(GREY_LUT, 105);
  writeCmd(CMD_GATE_V);
  writeData(GREY_LUT[105]);
  writeCmd(CMD_SOURCE_V);
  writeData(GREY_LUT[106]);
  writeData(GREY_LUT[107]);
  writeData(GREY_LUT[108]);
  writeCmd(CMD_VCOM);
  writeData(GREY_LUT[109]);
  writeCmd(CMD_BORDER);
  writeData(BORDER_GREY);

  // Rails up BEFORE the grey activation, in their own step. The vendor
  // sequences power down after every refresh, and the grey LUT's one-frame
  // phases would otherwise run while the booster is still ramping --
  // under-driven, pale mid-greys. CrossPoint found this the hard way.
  writeCmd(CMD_UPDATE_CTRL2);
  writeData(0xC0);
  writeCmd(CMD_ACTIVATE);
  waitBusy(3000);

  // Pass 2: the two planes, then the external-LUT activation.
  buildGreyPlane(packed, _fb, 1);  // lsb -> BW RAM
  setRamAreaFull();
  writeCmd(CMD_UPDATE_CTRL1);
  writeData(CTRL1_NORMAL);
  writeCmd(CMD_WRITE_BW);
  writeData(_fb, EPD_BUF_SIZE);
  buildGreyPlane(packed, _fb, 2);  // msb -> RED RAM
  writeCmd(CMD_WRITE_RED);
  writeData(_fb, EPD_BUF_SIZE);
  writeCmd(CMD_UPDATE_CTRL2);
  writeData(SEQ_GREY);
  writeCmd(CMD_ACTIVATE);
  waitBusy(8000);

  // Baseline resync, stock parity: the pass-1 B/W frame into both RAMs, the
  // border back to its resting value. There is no revert waveform on this
  // panel family; the next SEQ_FULL reloads the OTP LUT by itself.
  memcpy(_fb, _prev, EPD_BUF_SIZE);
  setRamAreaFull();
  writeCmd(CMD_WRITE_BW);
  writeData(_fb, EPD_BUF_SIZE);
  writeCmd(CMD_WRITE_RED);
  writeData(_fb, EPD_BUF_SIZE);
  writeCmd(CMD_BORDER);
  writeData(BORDER_INIT);

  // Grey is on the glass and the shadow describes the B/W pass, not it. The
  // next UI paint has to be a full clean, and displayPartial promotes itself.
  _firstPaint = true;
  _partialsSinceFull = 0;
  return true;
}

void Epd::deepSleep() {
  writeCmd(CMD_DEEP_SLEEP);
  writeData(0x03);
}
