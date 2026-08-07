#include "buzzer.h"

#include "board_pins.h"

namespace buzzer {
namespace {
bool g_enabled = true;

void tone_ms(int freq, int ms) {
  if (!g_enabled) return;
  ledcAttach(PIN_BUZZER, freq, 10);
  ledcWriteTone(PIN_BUZZER, freq);
  delay(ms);
  ledcWriteTone(PIN_BUZZER, 0);
  ledcDetach(PIN_BUZZER);
}
}  // namespace

void begin() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

void setEnabled(bool en) { g_enabled = en; }
bool enabled() { return g_enabled; }

void tap() { tone_ms(2400, 12); }
void confirm() { tone_ms(1800, 30); }
void error() {
  tone_ms(400, 60);
}
void win() {
  tone_ms(1319, 90);
  tone_ms(1568, 90);
  tone_ms(2093, 160);
}
}  // namespace buzzer
