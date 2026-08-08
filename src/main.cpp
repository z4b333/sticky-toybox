// Toybox — games and small tools for the Seeed reTerminal Sticky.
// ESP32-S3 + 3.97" 800x480 SSD1677 e-paper + GT911 touch.
//
// This file is the whole of what makes Toybox a firmware rather than a guest:
// power latch, panel, touch, sensors, the sleep policy, and the loop. Every
// screen lives in toybox-core/, which knows nothing about any of it.
#include <Arduino.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "board_pins.h"
#include "buzzer.h"
#include "epd.h"
#include "gfx.h"
#include "sensors.h"
#include "service.h"
#include "sticky_host.h"
#include "tools/lockscreen.h"
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

// --- idle sleep --------------------------------------------------------------
// A fridge magnet is left alone for hours at a time, and an ESP32-S3 awake with
// WiFi off still burns the battery flat in a couple of days. Anything that
// counts as use pushes this forward; when it runs out we take the same route as
// a held power button, so the panel keeps whatever was on it.
uint32_t g_lastActivity = 0;

// Below this the gauge is close enough to empty that a panel refresh can brown
// out mid-write, which is how e-paper ends up with half a frame on it for ever.
constexpr int LOW_BATTERY_PCT = 3;
uint32_t g_lastBatteryCheck = 0;

void noteActivity() { g_lastActivity = millis(); }

// --- what the lock screens ask the hardware for -------------------------------
// The core decides what the sleeping panel looks like; this is the only thing
// it cannot work out for itself. Anything absent stays absent -- an unset clock
// or a missing sensor shortens the line rather than filling it with zeroes.
void fillLockInfo(lock::Info& out) {
  sensors::Clock c;
  if (sensors::readClock(c)) {
    out.haveClock = true;
    out.hour = c.hour;
    out.minute = c.minute;
    out.day = c.day;
    out.month = c.month;
    out.year = c.year;
  }
  int deci, rh;
  if (sensors::readClimate(deci, rh)) {
    out.haveTemp = true;
    out.tempDeciC = deci;
  }
  const int pct = sensors::batteryPercent();
  if (pct >= 0) {
    out.haveBattery = true;
    out.batteryPct = pct;
    out.charging = sensors::charging();
  }
}

void setClockFromPhone(int64_t localEpochMs) {
  if (sensors::setClockFromEpochMs(localEpochMs)) Serial.println("clock set from phone");
}

// --- auto-rotate ---------------------------------------------------------------
// Only the pinned note follows the accelerometer. Every app screen is laid out
// for a 480x800 portrait canvas and would need reflowing, but a note is just
// text in a box -- it fills whatever shape it is given. A magnet on a fridge
// gets knocked sideways; this is the screen that should not care.
int g_pinnedRotation = 0;

void applyPinnedRotation() {
  if (!lock::config().autoRotate || !sensors::imuPresent()) return;
  const int want = sensors::orientation();
  if (want == g_pinnedRotation) return;
  g_pinnedRotation = want;
  epd.setRotation(want);
  touch.setRotation(want);
  epd.clear();
  drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
  epd.displayFull();  // a quarter turn changes every pixel; partial would smear
}

void showPinned() {
  epd.clear();
  drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
  epd.displayFull();
}

void powerOff(bool lowBattery = false) {
  epd.clear();
  // E-paper keeps its last image with no power. If a note is pinned, leave that
  // on the panel instead of a goodbye card — that is the whole point of a note
  // you stick on the fridge.
  ToolsCanvas& c = stickyHost.sharedCanvas();
  if (lowBattery) {
    // The one case that overrides everything else: if the panel just says
    // "shopping list" the owner has no idea why it stopped responding.
    c.textTrackedCentered(EPD_W / 2, 300, "BATTERY EMPTY", TS_LARGE, true, true, 3);
    c.textCentered(EPD_W / 2, 350, "plug in the USB-C cable to wake it", TS_MED, true);
  } else if (!drawPinnedFullScreen(c)) {
    // Nothing pinned: whatever the lock screen settings asked for. A panel that
    // holds an image with no power is a better clock than it is a goodbye card,
    // which is why that is the default.
    switch (lock::config().empty) {
      case lock::EMPTY_BLANK: break;  // a device that looks off, because it is
      case lock::EMPTY_GOODBYE:
        c.textTrackedCentered(EPD_W / 2, 200, "GOODBYE!", TS_HUGE, true, true, 4);
        c.textCentered(EPD_W / 2, 260, "press the power button to play again", TS_MED, true);
        break;
      default: lock::drawClock(c, lock::config(), lock::read()); break;
    }
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

// The power button does two things. Held, it always powers off. Released
// quickly it locks the pinned note again — but only from the pinned screen,
// because a stray press in the middle of a game should not end it.
void handlePowerButton() {
  if (digitalRead(PIN_BTN_OK) == LOW) {
    noteActivity();
    if (g_okDownSince == 0) g_okDownSince = millis();
    if (millis() - g_okDownSince > 2000) powerOff();
    return;
  }
  const bool wasDown = g_okDownSince != 0;
  const uint32_t held = wasDown ? millis() - g_okDownSince : 0;
  g_okDownSince = 0;
  if (wasDown && held > 40 && g_pinnedMode) powerOff();
}

// Sleeping while a timer is counting down would be a bug, not a saving, so a
// tool that wants ticks holds the device awake — the same rule the CrossPoint
// port uses for the reader's sleep timer.
void handleIdleSleep() {
  if (toybox.wantsTick()) {
    noteActivity();
    return;
  }
  const uint32_t idle = lock::sleepMs(lock::config());
  if (idle == 0) return;  // "never": the setting says stay awake
  if (millis() - g_lastActivity > idle) powerOff();
}

void handleLowBattery() {
  if (millis() - g_lastBatteryCheck < 60000) return;
  g_lastBatteryCheck = millis();
  if (sensors::charging()) return;
  const int pct = sensors::batteryPercent();
  if (pct >= 0 && pct <= LOW_BATTERY_PCT) powerOff(true);
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

  // Whatever the service screen last saved about this particular board, before
  // the first pixel is drawn or the first tap is read.
  svc::apply(svc::load());

  touch.begin();
  Serial.printf("touch: %s\n", touch.ok() ? "ok" : "NOT FOUND");
  sensors::begin();

  // Full-coverage font packs, if any have been installed (see gfx.h). Loaded
  // before the first paint so a pinned Chinese note wakes up whole.
  Serial.printf("font packs: %d faces\n", gfx::loadFontPacks());

  // Hold UP through power-on to correct the display and touch mapping. This is
  // the one screen that has to work when nothing else does, so it comes before
  // the shell starts and it is driven by buttons alone.
  // Also entered on its own when the touch controller did not answer: the
  // shell is unusable without it, and a device sitting on a hub it will never
  // respond to tells you nothing about why.
  if (svc::requested() || !touch.ok()) {
    Serial.println("service mode");
    svc::run();  // never returns
  }

  // The core draws the pinned footer and the notes portal receives the phone's
  // clock; neither knows what hardware is underneath, so the firmware hands
  // them the two functions that do.
  lock::apply(prefs);
  lock::setInfoHook(fillLockInfo);
  nweb::setClockHook(setClockFromPhone);

  toybox.begin(stickyHost);

  // Wait for the button that woke us to come back up, or the first loop would
  // read it as a fresh press and put the device straight back to sleep.
  while (digitalRead(PIN_BTN_OK) == LOW) delay(10);
  noteActivity();

  // Waking goes to whichever the settings say. With a note pinned the note is
  // usually the thing you came back to, but not everyone agrees, so it asks.
  char pinned[note::NAME_LEN + 1];
  if (note::getPinned(pinned) && lock::config().wake == lock::WAKE_NOTE) {
    g_pinnedMode = true;
    showPinned();
  } else {
    stickyHost.refresh(true);
  }
}

void loop() {
  TouchEvent ev;
  touch.poll(ev);
  if (ev.tapped || ev.swiped) noteActivity();

  if (g_pinnedMode) {
    if (ev.tapped) {
      if (PINNED_HUB.hit(ev.x, ev.y)) {
        // Apps are portrait-only; put the panel back before the hub draws.
        g_pinnedMode = false;
        g_pinnedRotation = 0;
        epd.setRotation(0);
        touch.setRotation(0);
        buzzer::confirm();
        stickyHost.refresh(true);
      } else if (tapPinnedFullScreen(stickyHost.sharedCanvas(), ev.x, ev.y)) {
        buzzer::tap();
        epd.clear();
        drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
        epd.displayPartial();
      }
    }
    applyPinnedRotation();
    handlePowerButton();
    handleLowBattery();
    handleIdleSleep();
    delay(20);
    return;
  }

  if (ev.tapped)
    toybox.onTap(ev.x, ev.y);
  else if (ev.swiped)
    toybox.onSwipe(ev.dx, ev.dy);
  if (toybox.wantsTick()) toybox.tick();

  handlePowerButton();
  handleLowBattery();
  handleIdleSleep();
  delay(20);
}
