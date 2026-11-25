// Battery charger enable (C6). See PIN_CHG_ENn in board_pins.h for why this
// exists: left alone, the pin floats high and the BQ25616 never charges.
//
// The level is latched with gpio_hold so it survives light sleep (where the
// S3 isolates its pins) and, on USB power, deep sleep after a power-off.
// While held, writes are ignored; allow() releases, writes and holds again.
#pragma once
#include <Arduino.h>
#include <driver/gpio.h>
#include "board_pins.h"

namespace charger {

inline bool& allowedRef() {
  static bool v = true;
  return v;
}

// true = charging permitted (pin LOW), false = charging off (pin HIGH).
inline void allow(bool on) {
  const gpio_num_t pin = (gpio_num_t)PIN_CHG_ENn;
  gpio_hold_dis(pin);
  pinMode(PIN_CHG_ENn, OUTPUT);
  digitalWrite(PIN_CHG_ENn, on ? LOW : HIGH);
  gpio_hold_en(pin);
  allowedRef() = on;
}

inline bool allowed() { return allowedRef(); }

// First thing in setup(), right after the power latch.
inline void begin() { allow(true); }

// Before esp_deep_sleep_start(): keep the held level while the chip sleeps on
// USB power. Charging is always re-allowed for sleep, because nothing will be
// awake to watch the 80 % limit.
inline void beforeDeepSleep() {
  allow(true);
  gpio_deep_sleep_hold_en();
}

}  // namespace charger
