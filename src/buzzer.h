// LEDC buzzer beeps (GPIO48). Non-blocking-ish: short beeps only.
#pragma once
#include <Arduino.h>

namespace buzzer {
void begin();
void setEnabled(bool en);
bool enabled();
void tap();      // subtle key click
void confirm();  // action accepted
void error();    // invalid action
void win();      // little jingle
}  // namespace buzzer
