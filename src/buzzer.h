// LEDC buzzer beeps (GPIO48). Non-blocking-ish: short beeps only.
//
// Volume is the PWM duty cycle rather than an amplifier. A piezo driven by a
// square wave is loudest at 50 % duty, where it spends the most time being
// pushed; narrower pulses excite it less and it is quieter. That is the whole
// mechanism, and it is why the steps below are not evenly spaced numbers -- a
// piezo's loudness against duty is nothing like linear, and these were chosen
// to sound like steps rather than to look like them.
#pragma once
#include <Arduino.h>

namespace buzzer {

// Named rather than a number, and deliberately not LOW/HIGH: those are Arduino
// macros, and a `Level::Low` that expands to `Level::0x0` is a bad afternoon.
enum class Level : uint8_t { Mute = 0, Low = 1, Med = 2, High = 3 };
inline constexpr int LEVEL_COUNT = 4;

void begin();
void setLevel(Level lv);
Level level();

// Kept because most callers only ever wanted "is there any sound at all".
void setEnabled(bool en);
bool enabled();

void tap();      // subtle key click
void confirm();  // action accepted
void error();    // invalid action
void win();      // little jingle

// Boot, once per flash: the first ever start of a device, and every start on a
// firmware it has not run before. See buzzer.cpp for why they differ.
void hello();
void updated();
}  // namespace buzzer
