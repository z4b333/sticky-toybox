// Toybox — games and small tools for the Seeed reTerminal Sticky.
// ESP32-S3 + 3.97" 800x480 SSD1677 e-paper + GT911 touch.
//
// This file is the whole of what makes Toybox a firmware rather than a guest:
// power latch, panel, touch, and the loop. Every screen lives in toybox-core/,
// which knows nothing about any of it.
#include <Arduino.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "board_pins.h"
#include "buzzer.h"
#include "epd.h"
#include "gfx.h"
#include "sticky_host.h"
#include "tools/tool_note.h"
#include "touch.h"

Preferences prefs;

namespace {
uint32_t g_okDownSince = 0;

// With a note pinned, waking goes straight to that note rather than to the hub:
// the thing on the fridge is the thing you came back to. Taps tick and cross
// its lines, the power button puts it back to sleep, and HUB is there for when
// you actually wanted the toys.
bool g_pinnedMode = false;

void showPinned() {
  epd.clear();
  drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
  epd.displayFull();
}

void powerOff() {
  epd.clear();
  // E-paper keeps its last image with no power. If a note is pinned, leave that
  // on the panel instead of a goodbye card — that is the whole point of a note
  // you stick on the fridge.
  ToolsCanvas& c = stickyHost.sharedCanvas();
  if (!drawPinnedFullScreen(c)) {
    c.textTrackedCentered(EPD_W / 2, 200, "GOODBYE!", TS_HUGE, true, true, 4);
    c.textCentered(EPD_W / 2, 260, "press the power button to play again", TS_MED, true);
  }
  epd.displayFull();
  epd.deepSleep();
  delay(50);

  // Release the power latch (battery operation powers down here)...
  digitalWrite(PIN_PWR_HOLD, LOW);
  digitalWrite(PIN_PWR_LOCK, LOW);
  delay(100);
  // ...and if USB keeps us alive, deep-sleep with wake on the power button.
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_OK, 0);
  esp_deep_sleep_start();
}

// The power button does two things now. Held, it always powers off. Released
// quickly it locks the pinned note again — but only from the pinned screen,
// because a stray press in the middle of a game should not end it.
void handlePowerButton() {
  if (digitalRead(PIN_BTN_OK) == LOW) {
    if (g_okDownSince == 0) g_okDownSince = millis();
    if (millis() - g_okDownSince > 2000) powerOff();
    return;
  }
  const bool wasDown = g_okDownSince != 0;
  const uint32_t held = wasDown ? millis() - g_okDownSince : 0;
  g_okDownSince = 0;
  if (wasDown && held > 40 && g_pinnedMode) powerOff();
}
}  // namespace

void setup() {
  // Power latch FIRST or the board dies when USB is unplugged.
  gpio_hold_dis((gpio_num_t)PIN_PWR_HOLD);
  gpio_hold_dis((gpio_num_t)PIN_PWR_LOCK);
  pinMode(PIN_PWR_HOLD, OUTPUT);
  pinMode(PIN_PWR_LOCK, OUTPUT);
  digitalWrite(PIN_PWR_HOLD, HIGH);
  digitalWrite(PIN_PWR_LOCK, HIGH);

  Serial.begin(115200);

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_OK, INPUT_PULLUP);

  prefs.begin("toybox", false);
  buzzer::begin();
  buzzer::setEnabled(prefs.getBool("sound", true));

  if (!epd.begin()) {
    Serial.println("EPD alloc failed");
    while (true) delay(1000);
  }
  touch.begin();
  Serial.printf("touch: %s\n", touch.ok() ? "ok" : "NOT FOUND");

  // Full-coverage font packs, if any have been installed (see gfx.h). Loaded
  // before the first paint so a pinned Chinese note wakes up whole.
  Serial.printf("font packs: %d faces\n", gfx::loadFontPacks());

  toybox.begin(stickyHost);

  // Wait for the button that woke us to come back up, or the first loop would
  // read it as a fresh press and put the device straight back to sleep.
  while (digitalRead(PIN_BTN_OK) == LOW) delay(10);

  char pinned[note::NAME_LEN + 1];
  if (note::getPinned(pinned)) {
    g_pinnedMode = true;
    showPinned();
  } else {
    stickyHost.refresh(true);
  }
}

void loop() {
  TouchEvent ev;
  touch.poll(ev);

  if (g_pinnedMode) {
    if (ev.tapped) {
      if (PINNED_HUB.hit(ev.x, ev.y)) {
        g_pinnedMode = false;
        buzzer::confirm();
        stickyHost.refresh(true);
      } else if (tapPinnedFullScreen(stickyHost.sharedCanvas(), ev.x, ev.y)) {
        buzzer::tap();
        epd.clear();
        drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
        epd.displayPartial();
      }
    }
    handlePowerButton();
    delay(20);
    return;
  }

  if (ev.tapped)
    toybox.onTap(ev.x, ev.y);
  else if (ev.swiped)
    toybox.onSwipe(ev.dx, ev.dy);
  if (toybox.wantsTick()) toybox.tick();

  handlePowerButton();
  delay(20);
}
