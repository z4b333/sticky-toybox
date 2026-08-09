#include "sensors.h"

#include "board_pins.h"

#ifndef TOYBOX_HOST
#include <Wire.h>
#endif

// Register maps, from the datasheets. Nothing here has met real hardware yet;
// if a chip misbehaves on the bench, docs/BRINGUP.md section 5b is the place
// its symptoms are catalogued.
namespace sensors {
namespace {

constexpr uint8_t ADDR_GAUGE = 0x55;   // BQ27220
constexpr uint8_t ADDR_RTC = 0x51;     // PCF8563
constexpr uint8_t ADDR_SHT = 0x44;     // SHT40
constexpr uint8_t ADDR_IMU = 0x6A;     // LSM6DS3TR-C, SA0 low

bool g_gauge = false, g_rtc = false, g_sht = false, g_imu = false;
int g_orientation = 0;

#ifndef TOYBOX_HOST

bool probe(uint8_t addr) {
  Wire1.beginTransmission(addr);
  return Wire1.endTransmission() == 0;
}

bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, int n) {
  Wire1.beginTransmission(addr);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if ((int)Wire1.requestFrom((int)addr, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire1.read();
  return true;
}

bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(addr);
  Wire1.write(reg);
  Wire1.write(val);
  return Wire1.endTransmission() == 0;
}

// BQ27220 standard commands are little-endian 16-bit reads.
int gauge16(uint8_t cmd) {
  uint8_t b[2];
  if (!readRegs(ADDR_GAUGE, cmd, b, 2)) return -1;
  return b[0] | (b[1] << 8);
}

uint8_t bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
uint8_t unbcd(uint8_t v) { return (uint8_t)(((v >> 4) * 10) + (v & 0x0F)); }

// SHT4x CRC-8, poly 0x31, init 0xFF.
uint8_t shtCrc(const uint8_t* d) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < 2; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

#endif  // !TOYBOX_HOST

}  // namespace

void begin() {
#ifndef TOYBOX_HOST
  Wire1.begin(PIN_SENS_SDA, PIN_SENS_SCL, 400000);
  g_gauge = probe(ADDR_GAUGE);
  g_rtc = probe(ADDR_RTC);
  g_sht = probe(ADDR_SHT);
  g_imu = probe(ADDR_IMU);
  pinMode(PIN_CHARGE_STATE, INPUT);
  if (g_imu) {
    // CTRL1_XL (0x10): 26 Hz, +/-2 g. Low rate is plenty for orientation and
    // keeps the chip near its floor current.
    writeReg(ADDR_IMU, 0x10, 0x20);
  }
  Serial.printf("sensors: gauge %d rtc %d sht %d imu %d\n", g_gauge, g_rtc, g_sht, g_imu);
#endif
}

bool batteryPresent() { return g_gauge; }
bool clockPresent() { return g_rtc; }
bool climatePresent() { return g_sht; }
bool imuPresent() { return g_imu; }

#ifdef TOYBOX_HOST

// The host harness runs with every sensor absent, plus setters the tests use
// to fake presence where a guard needs it.
int batteryPercent() { return -1; }
int batteryMillivolts() { return -1; }
bool charging() { return false; }
bool clockValid() { return false; }
bool readClock(Clock&) { return false; }
bool setClock(const Clock&) { return false; }
bool setClockFromEpochMs(int64_t) { return false; }
bool readClimate(int&, int&) { return false; }
bool readAccel(int&, int&) { return false; }
int orientation() { return g_orientation; }
void hostSetOrientation(int o) { g_orientation = o; }

#else

int batteryPercent() {
  if (!g_gauge) return -1;
  const int soc = gauge16(0x2C);  // StateOfCharge, percent
  return (soc >= 0 && soc <= 100) ? soc : -1;
}

int batteryMillivolts() {
  if (!g_gauge) return -1;
  const int mv = gauge16(0x08);  // Voltage, mV
  return (mv > 2000 && mv < 5000) ? mv : -1;
}

bool charging() {
  // CHARGE_STATE comes from the BQ25616 charger. Polarity is unverified on
  // hardware; if the icon reads backwards on the bench, flip here.
  return digitalRead(PIN_CHARGE_STATE) == LOW;
}

bool clockValid() {
  if (!g_rtc) return false;
  uint8_t sec;
  if (!readRegs(ADDR_RTC, 0x02, &sec, 1)) return false;
  return (sec & 0x80) == 0;  // VL flag: set = oscillator stopped, time is junk
}

bool readClock(Clock& out) {
  if (!g_rtc) return false;
  uint8_t b[7];
  if (!readRegs(ADDR_RTC, 0x02, b, 7)) return false;
  if (b[0] & 0x80) return false;  // VL: never been set (or battery blipped)
  out.second = unbcd(b[0] & 0x7F);
  out.minute = unbcd(b[1] & 0x7F);
  out.hour = unbcd(b[2] & 0x3F);
  out.day = unbcd(b[3] & 0x3F);
  out.month = unbcd(b[5] & 0x1F);
  out.year = (uint16_t)(2000 + unbcd(b[6]));
  return true;
}

bool setClock(const Clock& c) {
  if (!g_rtc) return false;
  Wire1.beginTransmission(ADDR_RTC);
  Wire1.write(0x02);
  Wire1.write(bcd(c.second));  // writing seconds also clears the VL flag
  Wire1.write(bcd(c.minute));
  Wire1.write(bcd(c.hour));
  Wire1.write(bcd(c.day));
  Wire1.write(0);  // weekday, unused
  Wire1.write(bcd(c.month));
  Wire1.write(bcd((uint8_t)(c.year % 100)));
  return Wire1.endTransmission() == 0;
}

bool setClockFromEpochMs(int64_t localEpochMs) {
  // Civil-from-days, Howard Hinnant's algorithm.
  int64_t secs = localEpochMs / 1000;
  int64_t days = secs / 86400;
  int rem = (int)(secs % 86400);
  if (rem < 0) { rem += 86400; days--; }
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = (unsigned)(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;

  Clock c;
  c.year = (uint16_t)(y + (m <= 2));
  c.month = (uint8_t)m;
  c.day = (uint8_t)d;
  c.hour = (uint8_t)(rem / 3600);
  c.minute = (uint8_t)((rem % 3600) / 60);
  c.second = (uint8_t)(rem % 60);
  return setClock(c);
}

bool readClimate(int& tempDeciC, int& rhPercent) {
  if (!g_sht) return false;
  Wire1.beginTransmission(ADDR_SHT);
  Wire1.write(0xFD);  // high-precision single shot
  if (Wire1.endTransmission() != 0) return false;
  delay(10);
  uint8_t b[6];
  if (Wire1.requestFrom((int)ADDR_SHT, 6) != 6) return false;
  for (int i = 0; i < 6; i++) b[i] = Wire1.read();
  if (shtCrc(b) != b[2] || shtCrc(b + 3) != b[5]) return false;
  const int tRaw = (b[0] << 8) | b[1];
  const int hRaw = (b[3] << 8) | b[4];
  tempDeciC = -450 + (int)((1750LL * tRaw) / 65535);
  rhPercent = -6 + (int)((125LL * hRaw) / 65535);
  if (rhPercent < 0) rhPercent = 0;
  if (rhPercent > 100) rhPercent = 100;
  return true;
}

bool readAccel(int& ax, int& ay) {
  if (!g_imu) return false;
  uint8_t b[4];
  if (!readRegs(ADDR_IMU, 0x28, b, 4)) return false;  // OUTX_L_XL
  ax = (int16_t)(b[0] | (b[1] << 8));
  ay = (int16_t)(b[2] | (b[3] << 8));
  return true;
}

int orientation() {
  int ax = 0, ay = 0;
  if (!readAccel(ax, ay)) return g_orientation;
  // Only change our mind when gravity clearly favours one axis: a device flat
  // on a table, or mid-turn, keeps the previous answer instead of flickering.
  const int TH = 8000;  // ~0.5 g at +/-2 g full scale
  // Axis-to-panel mapping is unverified on hardware; BRINGUP 5b covers
  // re-mapping if the panel rotates the wrong way on the bench.
  if (ay < -TH && abs(ax) < TH) g_orientation = 0;
  else if (ay > TH && abs(ax) < TH) g_orientation = 2;
  else if (ax < -TH && abs(ay) < TH) g_orientation = 1;
  else if (ax > TH && abs(ay) < TH) g_orientation = 3;
  return g_orientation;
}

#endif  // TOYBOX_HOST

}  // namespace sensors
