#include "epd.h"

#include <SPI.h>

#include "board_pins.h"
#include "sensors.h"

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

// 20 MHz: the SSD1677's write-clock ceiling (50 ns cycle). Every refresh
// streams ~96 KB (two 1-bit planes, or one 2 bpp frame on the SSD2677), which
// at the old, conservative 10 MHz was ~77 ms of every page turn.
const SPISettings kSpi(20000000, MSBFIRST, SPI_MODE0);
}  // namespace

bool Epd::begin() {
  // Both framebuffers go to PSRAM when there is any.
  //
  // They are 48 KB each and live for the whole run, so on a device with only
  // internal RAM they were 96 KB of permanently-occupied heap sitting in the
  // middle of everything else -- the reason a 32 KB inflate window, a 48 KB
  // page and an SD mount all found the heap in pieces rather than short. Out
  // in PSRAM they cost the internal heap nothing at all.
  //
  // PSRAM is slower per access, which matters here in one place: drawing is
  // per-pixel through fillRect. Against a panel refresh of 1.7 seconds it is
  // not expected to be visible, and if it ever is, the shadow buffer (_prev,
  // which is only ever memcpy'd and streamed out) can stay out there while
  // the live one comes home.
  //
  // ps_malloc falls back to internal automatically when there is no PSRAM, so
  // a board without it behaves exactly as before.
  _fb = (uint8_t*)ps_malloc(EPD_BUF_SIZE);
  _prev = (uint8_t*)ps_malloc(EPD_BUF_SIZE);
  if (!_fb) _fb = (uint8_t*)malloc(EPD_BUF_SIZE);
  if (!_prev) _prev = (uint8_t*)malloc(EPD_BUF_SIZE);
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

  // Which controller is this? Seeed moved the Sticky from the SSD1677 to the
  // SSD2677 for supply reasons, and the two share nothing past the pins: a
  // different command set, two bits a pixel instead of one, and a BUSY line
  // that means the opposite thing.
  //
  // Two questions, either of which is enough:
  //  1. Seeed's own: command 0x70, one byte back, 0x07 is the SSD2677. The
  //     panel answers on its one data line (SDA, our MOSI) -- reading MISO,
  //     which is the SD card's, got 0x00 from a real SSD2677.
  //  2. Where BUSY rests after a reset. The SSD2677's is active LOW, so idle
  //     it sits HIGH; the SSD1677's is active HIGH and idles LOW once its
  //     reset is done. Settled for 100 ms either way.
  // A board with no panel at all leaves BUSY wherever it floats, and may come
  // out as an SSD2677 -- which shows nothing, as either guess would.
  _chip = Chip::SSD2677;  // so reset() holds the line low long enough for either
  reset();
  _chipId = readChipId();
  delay(100);
  _busyIdleHigh = digitalRead(PIN_EPD_BUSY) == HIGH;
  _chip = (_chipId == 0x07 || _busyIdleHigh) ? Chip::SSD2677 : Chip::SSD1677;

  if (_chip == Chip::SSD2677) {
    s2Init();
    _panelOk = s2Ready();
  } else {
    // Exactly the sequence every SSD1677 unit has booted with until now: the
    // chip-ID question above was the only thing added in front of it.
    reset();
    _panelOk = probePanel();
    initController();
  }
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
  if (_chip == Chip::SSD2677) {
    s2Init();
    _panelOk = s2Ready();
  } else {
    reset();
    _panelOk = probePanel();
    initController();
  }
  _firstPaint = true;  // the controller's RAM is gone; the next paint must be full
}

void Epd::reset() {
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
  digitalWrite(PIN_EPD_RST, LOW);
  delay(_chip == Chip::SSD2677 ? 20 : 2);  // Seeed's reference holds the SSD2677 for 20 ms
  _s2Powered = false;                      // a reset drops the panel's high voltage
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

// BUSY is active HIGH on the SSD1677 and active LOW on the SSD2677.
bool Epd::busy() const {
  const int level = digitalRead(PIN_EPD_BUSY);
  return _chip == Chip::SSD2677 ? level == LOW : level == HIGH;
}

// The chip ID, read back over the panel's one data line. The panel has no
// MISO: SDA turns round and the panel drives it for a read (3-wire SPI), which
// the Arduino SPI class cannot do, so the bus is let go and the byte is
// clocked in by hand, then the bus is taken back. Boot only, before the SD
// card has been near the bus. Clocked slowly; this is one byte.
uint8_t Epd::readChipId() {
  writeCmd(0x70);
  return read3w();
}

// The read half: one byte clocked in on SDA after the command that asks for it.
uint8_t Epd::read3w() {
  SPI.end();  // give SCK and MOSI back as plain GPIOs
  pinMode(PIN_EPD_SCK, OUTPUT);
  digitalWrite(PIN_EPD_SCK, LOW);
  pinMode(PIN_EPD_MOSI, INPUT);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  delayMicroseconds(5);
  uint8_t v = 0;
  for (int i = 0; i < 8; i++) {
    digitalWrite(PIN_EPD_SCK, HIGH);  // mode 0: sample on the rising edge
    delayMicroseconds(5);
    v = (uint8_t)((v << 1) | (digitalRead(PIN_EPD_MOSI) ? 1 : 0));
    digitalWrite(PIN_EPD_SCK, LOW);
    delayMicroseconds(5);
  }
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  return v;
}

void Epd::waitBusy(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (busy()) {
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

void Epd::dim(uint8_t level) {
  // A set bit is white, so this takes ink away and leaves the rest as it was.
  // The steps are how much survives: everything, three quarters, a half, a
  // quarter. Which one suits is taste -- a picture wants the page faint behind
  // it, a reader wants to still see what they were reading -- so the lock
  // screen makes it a setting rather than a decision taken here.
  if (!level) return;
  for (int y = 0; y < PANEL_H; y++) {
    uint8_t mask;
    switch (level) {
      case 1: mask = (y & 1) ? 0x22 : 0x88; break;   // a quarter of the ink goes
      case 2: mask = (y & 1) ? 0x55 : 0xAA; break;   // half of it
      default: mask = (y & 1) ? 0xFF : 0x55; break;  // three quarters
    }
    uint8_t* row = &_fb[(uint32_t)y * EPD_WB];
    for (int x = 0; x < EPD_WB; x++) row[x] |= mask;
  }
}

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
  if (_chip == Chip::SSD2677) {
    s2Refresh(S2Mode::Full, false);
    memcpy(_prev, _fb, EPD_BUF_SIZE);
    _firstPaint = false;
    _partialsSinceFull = 0;
    return;
  }
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
  if (_chip == Chip::SSD2677) {
    s2Refresh(S2Mode::Partial, true);
    memcpy(_prev, _fb, EPD_BUF_SIZE);
    _partialsSinceFull++;
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
// One row of a 2-bit page: 480 pixels, four to a byte.
static constexpr int GREY_ROW_BYTES = EPD_W / 4;   // 120
static constexpr int GREY_BAND_ROWS = 40;          // 4,800 bytes a read
// Static rather than stack: the loop task has 8 KB and this is 4.8 of it.
// Static rather than heap: a page that streams to avoid a big allocation
// should not need one to do it. One buffer for every user in this file (the
// grey plane builders and the SSD2677's row streamer), which never overlap.
static uint8_t s_band[GREY_BAND_ROWS * GREY_ROW_BYTES];

void Epd::buildGreyPlane(GreySource& src, uint8_t* dst, int which) {
  memset(dst, 0x00, EPD_BUF_SIZE);
  uint8_t* const band = s_band;
  for (int y0 = 0; y0 < EPD_H; y0 += GREY_BAND_ROWS) {
    const int rows = (EPD_H - y0) < GREY_BAND_ROWS ? (EPD_H - y0) : GREY_BAND_ROWS;
    if (!src.read((uint32_t)y0 * GREY_ROW_BYTES, band, (uint32_t)rows * GREY_ROW_BYTES)) return;
    for (int r = 0; r < rows; r++) {
      const uint8_t* row = band + (size_t)r * GREY_ROW_BYTES;
      const int y = y0 + r;
      for (int x = 0; x < EPD_W; x++) {
        const uint8_t lv = (row[x >> 2] >> (6 - 2 * (x & 3))) & 3;
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
}

namespace {
// The old caller's page, wearing the new interface. Nothing about the
// sequence below knows or cares which one it got.
struct MemGrey : Epd::GreySource {
  const uint8_t* p;
  explicit MemGrey(const uint8_t* src) : p(src) {}
  bool read(uint32_t off, uint8_t* dst, uint32_t n) override {
    memcpy(dst, p + off, n);
    return true;
  }
};
}  // namespace

bool Epd::displayGrey2bpp(const uint8_t* packed) {
  if (!packed) return false;
  MemGrey src(packed);
  return displayGrey2bpp(src);
}

bool Epd::displayGrey2bpp(GreySource& packed) {
  if (!_fb || !_prev) return false;
  if (_chip == Chip::SSD2677) return s2Grey(packed);

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
  if (_chip == Chip::SSD2677) {
    if (_s2Powered) {  // high voltage off first, as Seeed's reference does
      writeCmd(0x02);
      writeData(0x00);
      waitBusy(30000);
      _s2Powered = false;
    }
    writeCmd(0x07);
    writeData(0xA5);
    _s2Mode = S2Mode::None;
    return;
  }
  writeCmd(CMD_DEEP_SLEEP);
  writeData(0x03);
}

// --- SSD2677 ------------------------------------------------------------------
// Ported from Seeed's reference driver for the replacement controller
// (driver/ssd2677.c and the chip probe in epaper_panel.c of their SEEED_EPAPER
// component). Register values are carried over byte for byte; what is Toybox's
// own is only how the framebuffer gets there.
//
// The controller takes two bits a pixel through 0x10, always the whole frame
// (96,000 bytes) -- there is no RAM window. For black and white the pair is
// widened from the 1-bit framebuffer on the way out: 11 white, 00 black for a
// full refresh, and (previous bit, current bit) for a partial one, which is how
// it diffs. Nothing extra is held in memory: the frame streams out five rows at
// a time.
//
// Rows go out bottom first. The SSD1677 path addresses Y from the far end (see
// setRamAreaFull), and Seeed's reference drives both chips so that the same
// buffer lands the same way up, so this keeps an image on the new panel where
// it sits on the old one. If a board says otherwise, the service screen's two
// flips still correct it.
namespace {
constexpr uint32_t S2_TIMEOUT = 30000;  // Seeed's busy timeout for this chip
constexpr int S2_ROW = PANEL_W / 4;     // 200 bytes a panel row at two bits a pixel
constexpr int S2_ROWS_PER_WRITE = 5;    // 1,000 bytes a transaction
// Toybox's grey levels (0 black .. 3 white) as the SSD2677 codes them. Seeed's
// reference swaps the middle two when it maps a 4-bit level, so a dark grey is
// 10 and a light grey 01. If the two mid-greys come out the wrong way round on
// the glass, this is the line to change: {0, 1, 2, 3}.
constexpr uint8_t S2_GREY[4] = {0x0, 0x2, 0x1, 0x3};

// Eight 1-bit pixels to eight 2-bit ones: each set bit (white) becomes 11.
inline void s2Widen(uint8_t mono, uint8_t* out) {
  uint16_t w = 0;
  for (int i = 0; i < 8; i++)
    if (mono & (0x80 >> i)) w |= (uint16_t)0x3 << (14 - 2 * i);
  out[0] = (uint8_t)(w >> 8);
  out[1] = (uint8_t)w;
}

// Previous and current frame interleaved: each pixel is (previous, current).
inline void s2Interleave(uint8_t prev, uint8_t cur, uint8_t* out) {
  uint16_t w = 0;
  for (int i = 0; i < 8; i++) {
    const uint8_t pair = (uint8_t)((((prev >> (7 - i)) & 1) << 1) | ((cur >> (7 - i)) & 1));
    w |= (uint16_t)pair << (14 - 2 * i);
  }
  out[0] = (uint8_t)(w >> 8);
  out[1] = (uint8_t)w;
}

// Waveform picks by panel temperature, from Seeed's reference (their notes cite
// the 3D97 V2 vendor demo).
uint8_t s2FullWaveform(uint8_t t) {
  if (t <= 10) return 0xE8;
  if (t <= 20) return 0xEB;
  if (t <= 30) return 0xEE;
  if (t <= 127) return 0xF1;
  return 0xE8;  // above 127 is a negative reading
}
uint8_t s2GreyWaveform(uint8_t t) {
  if (t <= 15) return 0xF1;
  if (t <= 127) return 0xFA;
  return 0xF1;
}
uint8_t s2PartialWaveform(uint8_t t) {
  if (t <= 10) return 0x08;
  if (t <= 20) return 0x0F;
  if (t <= 30) return 0x19;
  if (t <= 127) return 0x23;
  return 0x08;
}
}  // namespace

// Power-on setup. Includes the reset, so it is also how the controller is put
// back after anything else has had the bus.
void Epd::s2Init() {
  static const uint8_t psr[] = {0x2F, 0x0E};
  static const uint8_t booster[] = {0x0F, 0x8B, 0x93, 0xC1};
  static const uint8_t timing[] = {0x76, 0x76, 0x76, 0x5A, 0x9D, 0x8A, 0x76, 0x62};
  static const uint8_t resolution[] = {0x03, 0x20, 0x02, 0xA8};
  static const uint8_t gsst[] = {0x00, 0x00, 0x00, 0x00};

  reset();
  waitBusy(S2_TIMEOUT);
  writeCmd(0x00);  // panel setting
  writeData(psr, sizeof(psr));
  waitBusy(S2_TIMEOUT);
  writeCmd(0x06);  // booster
  writeData(booster, sizeof(booster));
  writeCmd(0xE7);
  writeData(0xC1);
  writeCmd(0x30);  // PLL
  writeData(0x08);
  writeCmd(0x50);  // VCOM and data interval
  writeData(0x77);
  writeCmd(0x62);
  writeData(timing, sizeof(timing));
  writeCmd(0x61);  // resolution
  writeData(resolution, sizeof(resolution));
  writeCmd(0xE0);
  writeData(0x10);
  writeCmd(0x65);  // gate source start
  writeData(gsst, sizeof(gsst));
  writeCmd(0xE9);
  writeData(0x01);
  waitBusy(S2_TIMEOUT);
  _s2Powered = false;
  _s2Mode = S2Mode::None;
}

// The SSD2677 answer to probePanel(): after a reset its BUSY line has to come
// back to idle. Not getting there in Seeed's own timeout means nobody is home.
bool Epd::s2Ready() {
  const uint32_t t0 = millis();
  while (busy()) {
    if (millis() - t0 > S2_TIMEOUT) return false;
    delay(1);
  }
  return true;
}

// The temperature the waveform is chosen by, in whole degrees, 0..127.
//
// Seeed's reference can ask the panel's own sensor (command 0x40), but that is
// a read, and on this board a read means letting go of a bus the SD card may
// be holding mid-book. It also takes an external figure instead, which is what
// this does: the SHT40 on the sensor bus. Before the sensors are up (the first
// paint) or if the SHT40 is missing, room temperature -- the 21-30 bucket.
// Checked on the first sample unit: SHT40 30.1 C against the panel's own 31 C
// at the same moment, so the board sensor does not run warm enough to matter.
uint8_t Epd::s2Temperature() {
  int deci = 0, rh = 0;
  if (!sensors::readClimate(deci, rh)) return 25;
  if (deci <= 0) return 0;  // Seeed clamps the same way
  const int c = (deci + 5) / 10;
  return (uint8_t)(c > 127 ? 127 : c);
}

// Load the waveform for this refresh, chosen by temperature.
void Epd::s2Latch(S2Mode m) {
  const uint8_t t = s2Temperature();
  uint8_t wf;
  switch (m) {
    case S2Mode::Partial: wf = s2PartialWaveform(t); break;
    case S2Mode::Grey: wf = s2GreyWaveform(t); break;
    default: wf = s2FullWaveform(t); break;
  }
  writeCmd(0xE0);
  writeData(0x12);
  writeCmd(0xE6);
  writeData(wf);
  writeCmd(0xA5);
  waitBusy(S2_TIMEOUT);
  delay(10);
}

// The framebuffer out through 0x10, widened to two bits a pixel on the way.
void Epd::s2StreamMono(bool diff) {
  static_assert(S2_ROW * S2_ROWS_PER_WRITE <= sizeof(s_band), "row batch must fit the shared band");
  uint8_t* const out = s_band;
  writeCmd(0x10);
  for (int y0 = PANEL_H - 1; y0 >= 0; y0 -= S2_ROWS_PER_WRITE) {
    int n = 0;
    for (int r = 0; r < S2_ROWS_PER_WRITE && y0 - r >= 0; r++, n++) {
      const uint32_t row = (uint32_t)(y0 - r) * EPD_WB;
      uint8_t* o = out + n * S2_ROW;
      for (int b = 0; b < EPD_WB; b++) {
        if (diff)
          s2Interleave(_prev[row + b], _fb[row + b], o + 2 * b);
        else
          s2Widen(_fb[row + b], o + 2 * b);
      }
    }
    writeData(out, (uint32_t)n * S2_ROW);
  }
}

// Power on if needed, refresh, and power off again unless this was a partial:
// Seeed's reference keeps the high voltage up across a run of partials.
void Epd::s2Drive(S2Mode m) {
  if (!_s2Powered) {
    writeCmd(0x04);
    waitBusy(S2_TIMEOUT);
    _s2Powered = true;
  }
  writeCmd(0x12);
  writeData(0x00);
  waitBusy(S2_TIMEOUT);
  if (m != S2Mode::Partial) {
    writeCmd(0x02);
    writeData(0x00);
    waitBusy(S2_TIMEOUT);
    _s2Powered = false;
  }
}

// One black-and-white refresh. Seeed's reference re-runs the power-on setup
// whenever the refresh kind changes (full, partial, grey), so this does too.
void Epd::s2Refresh(S2Mode m, bool diff) {
  if (_s2Mode != m) {
    s2Init();
    _s2Mode = m;
  }
  s2Latch(m);
  s2StreamMono(diff);
  s2Drive(m);
}

// Four-level grey, which this controller does natively: no custom LUT, no
// second pass. The page is mapped into panel space at two bits a pixel, which
// is 96,000 bytes -- exactly both framebuffers, so the top half of the panel is
// built in _fb and the bottom half in _prev and nothing is allocated. The
// shadow is gone afterwards, as on the SSD1677 path, so the next paint is full.
bool Epd::s2Grey(GreySource& src) {
  constexpr int HALF = PANEL_H / 2;  // 240 rows a buffer: 240 x 200 = 48,000
  memset(_fb, 0xFF, EPD_BUF_SIZE);
  memset(_prev, 0xFF, EPD_BUF_SIZE);
  uint8_t* const band = s_band;
  for (int y0 = 0; y0 < EPD_H; y0 += GREY_BAND_ROWS) {
    const int rows = (EPD_H - y0) < GREY_BAND_ROWS ? (EPD_H - y0) : GREY_BAND_ROWS;
    if (!src.read((uint32_t)y0 * GREY_ROW_BYTES, band, (uint32_t)rows * GREY_ROW_BYTES)) {
      _firstPaint = true;  // both buffers hold half a page now
      return false;
    }
    for (int r = 0; r < rows; r++) {
      const uint8_t* row = band + (size_t)r * GREY_ROW_BYTES;
      for (int x = 0; x < EPD_W; x++) {
        const uint8_t lv = (row[x >> 2] >> (6 - 2 * (x & 3))) & 3;
        int px, py;
        epdMapPixel(0, _flipX, _flipY, x, y0 + r, px, py);
        uint8_t* half = py < HALF ? _fb : _prev;
        uint8_t* p = &half[(uint32_t)(py % HALF) * S2_ROW + (px >> 2)];
        const int sh = 6 - 2 * (px & 3);
        *p = (uint8_t)((*p & ~(0x3 << sh)) | (S2_GREY[lv] << sh));
      }
    }
  }

  if (_s2Mode != S2Mode::Grey) {
    s2Init();
    _s2Mode = S2Mode::Grey;
  }
  s2Latch(S2Mode::Grey);
  writeCmd(0x10);
  for (int y = PANEL_H - 1; y >= 0; y--) {  // bottom first, as s2StreamMono
    const uint8_t* half = y < HALF ? _fb : _prev;
    writeData(half + (uint32_t)(y % HALF) * S2_ROW, S2_ROW);
  }
  s2Drive(S2Mode::Grey);

  // Leave the framebuffers as the SSD1677 path does: the page in black and
  // white, white only where it was white, in both. Reads the page once more.
  buildGreyPlane(src, _fb, 0);
  memcpy(_prev, _fb, EPD_BUF_SIZE);
  _firstPaint = true;
  _partialsSinceFull = 0;
  return true;
}
