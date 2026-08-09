// The four quiet chips on the sensor bus: fuel gauge, real-time clock,
// temperature/humidity, accelerometer.
//
// None of these have been exercised on real hardware. Every driver probes its
// chip in begin() and, if nothing answers, reports absent and returns safe
// values from then on. A missing sensor turns its feature off; it never hangs
// the bus or the boot.
//
// Bus map (from the board profile): the BQ27220 gauge, PCF8563 RTC, SHT40 and
// LSM6DS3TR-C all share one I2C bus on SDA=GPIO1 / SCL=GPIO0, run at 400 kHz.
// That is Wire1 here, because the GT911 touch owns Wire (SDA3/SCL2). GPIO0 is
// a strapping pin; the bus is only initialised after boot, which is safe.
#pragma once
#include <Arduino.h>

namespace sensors {

// Starts Wire1 and probes all four chips. Call once in setup().
void begin();

// --- battery (BQ27220) ---------------------------------------------------
bool batteryPresent();
int batteryPercent();      // 0..100, or -1 when absent
#ifdef TOYBOX_HOST
// Harness only: pretend a gauge answered, so the screens that draw one can be
// rendered and checked.
void hostSetBattery(int pct, bool charging);
#endif
int batteryMillivolts();   // or -1
bool charging();           // CHARGE_STATE pin from the charger, gauge or not

// --- clock (PCF8563) -----------------------------------------------------
struct Clock {
  uint16_t year;  // e.g. 2026
  uint8_t month;  // 1..12
  uint8_t day;    // 1..31
  uint8_t hour;   // 0..23
  uint8_t minute;
  uint8_t second;
};
bool clockPresent();
bool clockValid();               // false until it has been set at least once
bool readClock(Clock& out);
bool setClock(const Clock& c);   // also marks it valid
// Convenience for the pairing pages: milliseconds since the Unix epoch,
// already shifted to local time by the phone.
bool setClockFromEpochMs(int64_t localEpochMs);

// --- temperature / humidity (SHT40) --------------------------------------
bool climatePresent();
// Blocking single measurement, ~10 ms. Returns false when absent or on a
// checksum error. Temperature in tenths of a degree C, humidity in percent.
bool readClimate(int& tempDeciC, int& rhPercent);

// --- accelerometer (LSM6DS3TR-C) ------------------------------------------
bool imuPresent();
// Raw counts on the two axes orientation() uses, at +/-2 g full scale, so
// 16384 counts is one g. The service screen shows these beside the rotation
// they produce: which way the chip is glued down is the one thing about this
// board no datasheet answers, and four readings from a device being turned
// settle it in a way that guessing at signs never has.
bool readAccel(int& ax, int& ay);
// Which edge of the panel currently points down, as a display rotation:
// 0 = normal portrait, 1 = 90 cw, 2 = upside down, 3 = 90 ccw.
// Returns the last confident answer; a flat-on-the-table device keeps
// whatever orientation it had.
int orientation();

}  // namespace sensors
