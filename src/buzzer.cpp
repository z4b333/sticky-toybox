#include "buzzer.h"

#include "board_pins.h"

namespace buzzer {
namespace {
Level g_level = Level::High;

// Duty out of 1024, at 10-bit resolution. 512 is a 50 % square wave, which is
// as loud as this pin can drive the piezo. The two below it are not 1/2 and 1/4
// of that: loudness falls away slowly at first and then quickly, so the steps
// have to bunch toward the bottom to sound evenly spaced. Ears, not arithmetic.
uint32_t dutyFor(Level lv) {
  switch (lv) {
    case Level::Low: return 24;
    case Level::Med: return 110;
    case Level::High: return 512;
    default: return 0;
  }
}

void tone_ms(int freq, int ms) {
  const uint32_t duty = dutyFor(g_level);
  if (duty == 0) return;
  ledcAttach(PIN_BUZZER, freq, 10);
  ledcWrite(PIN_BUZZER, duty);
  delay(ms);
  ledcWrite(PIN_BUZZER, 0);
  ledcDetach(PIN_BUZZER);
}
}  // namespace

void begin() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

void setLevel(Level lv) { g_level = lv; }
Level level() { return g_level; }

void setEnabled(bool en) { g_level = en ? Level::High : Level::Mute; }
bool enabled() { return g_level != Level::Mute; }

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

// The two boot sounds. A flash is done blind -- the panel is dark for several
// seconds while the browser writes, and the first thing anyone does is stare at
// it wondering whether the write took. These play the moment the welcome card
// is on the glass, before it waits for a tap, so the answer arrives in the ear
// as well as on the screen.
//
// They are deliberately different from each other, and from win(): a device
// that has just been wiped clean should not sound like one that kept your
// notes. hello() is a full rising arpeggio, unhurried, played once in a
// device's life; updated() is the top two notes of it, quick, because it comes
// back after every flash and a fanfare would wear out.
void hello() {
  tone_ms(523, 110);   // C5
  tone_ms(659, 110);   // E5
  tone_ms(784, 110);   // G5
  tone_ms(1047, 240);  // C6
}

void updated() {
  tone_ms(784, 80);   // G5
  tone_ms(1047, 150); // C6
}
}  // namespace buzzer
