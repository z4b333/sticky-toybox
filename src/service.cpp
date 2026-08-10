// The button loop behind service_ui.h. Firmware only: it reads GPIOs, writes
// NVS and restarts the chip, none of which the preview harness has.
//
// It never returns. You leave it by saving (which restarts) or by pulling the
// power, which is the right shape for a screen whose whole job is to fix the
// thing that stops you leaving any other way.
#include "service.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#include "board_pins.h"
#include "buzzer.h"
#include "epd.h"
#include "gfx.h"
#include "sdcard.h"
#include "sensors.h"
#include "service_ui.h"
#include "sticky_host.h"
#include "touch.h"

extern Preferences prefs;

namespace svc {
namespace {

// Held during the first moment of boot, before anything that could be broken
// has had to work. UP is the one button that does nothing else.
constexpr uint32_t HOLD_TO_SAVE_MS = 1500;
constexpr uint32_t REPEAT_MS = 400;

bool down(int pin) { return digitalRead(pin) == LOW; }

// Non-const because the tilt line has to be current: the whole point of it is
// that you turn the device, press a button, and read what the chip now says.
void paint(Report& r, const Config& cfg, int sel, bool haveCross, int cx, int cy,
           bool full) {
  if (r.imu) {
    sensors::readAccel(r.accelX, r.accelY);
    r.orientation = sensors::orientation();
  }
  epd.clear();
  render(stickyHost.sharedCanvas(), r, cfg, sel, haveCross, cx, cy);
  if (full)
    epd.displayFull();
  else
    epd.displayPartial();
}

}  // namespace

Config load() {
  Config c;
  // Both flips default on. The first board to run this needed the panel turned
  // half a turn -- mirrored and upside down together -- and that is the scan
  // direction of the panel, not a quirk of one unit, so a fresh device should
  // come up right rather than making its owner discover it. Anything already
  // saved still wins over these.
  c.flipX = prefs.getBool("hw_fx", true);
  c.flipY = prefs.getBool("hw_fy", true);
  c.swapXY = prefs.getBool("hw_tsw", true);
  c.tFlipX = prefs.getBool("hw_tfx", true);
  c.tFlipY = prefs.getBool("hw_tfy", true);
  return c;
}

void apply(const Config& c) {
  epd.setPanelFlip(c.flipX, c.flipY);
  touch.setPanelFlip(c.flipX, c.flipY);
  touch.setMapping(c.swapXY, c.tFlipX, c.tFlipY);
}

void save(const Config& c) {
  prefs.putBool("hw_fx", c.flipX);
  prefs.putBool("hw_fy", c.flipY);
  prefs.putBool("hw_tsw", c.swapXY);
  prefs.putBool("hw_tfx", c.tFlipX);
  prefs.putBool("hw_tfy", c.tFlipY);
}

// Either button, because which GPIO is UP and which is DOWN is itself one of
// the unverified guesses. Needing the right one to reach the screen that fixes
// wrong guesses would be a poor joke.
bool requested() { return down(PIN_BTN_UP) || down(PIN_BTN_DOWN); }

void run() {
  Report r;
  r.panelOk = epd.panelAnswered();
  r.touchOk = touch.ok();
  r.touchAddr = touch.address();
  r.gauge = sensors::batteryPresent();
  r.rtc = sensors::clockPresent();
  r.sht = sensors::climatePresent();
  r.imu = sensors::imuPresent();
  r.battMv = sensors::batteryMillivolts();
  r.fontFaces = gfx::loadedFaceCount();
  r.psramKb = (uint32_t)(ESP.getPsramSize() / 1024);
#ifdef TB_VERSION
  r.version = "toybox " TB_VERSION "  ·  " TB_DATE;
#else
  r.version = "toybox  " __DATE__ " " __TIME__;
#endif

  Config cfg = load();
  apply(cfg);

  int sel = 0;
  bool haveCross = false;
  int cx = 0, cy = 0;
  paint(r, cfg, sel, haveCross, cx, cy, true);

  uint32_t okDownSince = 0;
  uint32_t lastRepeat = 0;
  bool upWas = false, downWas = false;

  for (;;) {
    // --- UP / DOWN move the selection, and repeat if held ------------------
    const bool upNow = down(PIN_BTN_UP);
    const bool dnNow = down(PIN_BTN_DOWN);
    const bool fresh = (upNow && !upWas) || (dnNow && !downWas);
    const bool repeat = (upNow || dnNow) && millis() - lastRepeat > REPEAT_MS;
    if (fresh || repeat) {
      lastRepeat = millis();
      sel += upNow ? -1 : 1;
      if (sel < 0) sel = ROWS - 1;
      if (sel >= ROWS) sel = 0;
      haveCross = false;
      buzzer::tap();
      paint(r, cfg, sel, haveCross, cx, cy, false);
    }
    upWas = upNow;
    downWas = dnNow;

    // --- OK: tap changes the selected row, hold saves ----------------------
    if (down(PIN_BTN_OK)) {
      if (okDownSince == 0) okDownSince = millis();
      if (millis() - okDownSince > HOLD_TO_SAVE_MS) {
        save(cfg);
        epd.clear();
        ToolsCanvas& c = stickyHost.sharedCanvas();
        c.textTrackedCentered(c.width() / 2, 340, "SAVED", TS_HUGE, true, true, 4);
        c.textCentered(c.width() / 2, 400, "restarting", TS_MED, true);
        epd.displayFull();
        delay(400);
        esp_restart();
      }
    } else if (okDownSince != 0) {
      okDownSince = 0;
      if (sel == ROW_SAVE) {
        save(cfg);
        esp_restart();
      } else if (sel == ROW_SD) {
        // Runs on demand only. Two devices on one bus is the least-tested thing
        // in this project, and a probe that ran at every boot would be taking
        // that risk on behalf of somebody who only wanted to fix their screen.
        const sdcard::Report sd = sdcard::probe();
        r.sdTried = true;
        r.sdMounted = sd.mounted;
        r.sdSizeMb = sd.sizeMb;
        r.sdFiles = sd.files;
        r.sdKbPerSec = sd.readKbPerSec;
        r.sdPanelOk = sd.panelSurvived;
        r.sdFailedAt = sd.failedAt;
        buzzer::confirm();
        // Full, because the probe resets the controller: its RAM is gone and a
        // differential update would be differencing against nothing.
        paint(r, cfg, sel, haveCross, cx, cy, true);
      } else if (sel != ROW_TEST) {
        toggleRow(cfg, sel);
        apply(cfg);
        buzzer::confirm();
        // A flip changes every pixel on the panel, so a differential update
        // would leave the old image showing through the new one.
        paint(r, cfg, sel, haveCross, cx, cy, sel <= ROW_FLIP_Y);
      }
    }

    // --- the test ----------------------------------------------------------
    TouchEvent ev;
    touch.poll(ev);
    if ((ev.tapped || ev.swiped) && sel == ROW_TEST) {
      haveCross = true;
      cx = ev.x;
      cy = ev.y;
      buzzer::tap();
      paint(r, cfg, sel, haveCross, cx, cy, false);
    }

    delay(20);
  }
}

}  // namespace svc
