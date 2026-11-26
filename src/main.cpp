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
#include "charger.h"
#include <esp_system.h>

#include "bmp_gray.h"
#include "board_pins.h"
#include "buzzer.h"
#include "epd.h"
#include "gfx.h"
#include "cardfonts.h"
#include "sdcard.h"
#include "ota.h"
#include "sensors.h"
#include "service.h"
#include "chrome.h"
#include "sticky_host.h"
#include "tools/book_thumbs.h"
#include "tools/charge_dash.h"
#include "tools/climate_calc.h"
#include "tools/lock_image.h"
#include "tools/lockscreen.h"
#include "tools/tool_note.h"
#include "touch.h"
#include "tour.h"
#include "welcome.h"

Preferences prefs;

namespace {
// --- talking to the outside ---------------------------------------------------
// The build points Serial at the S3's own USB, but this board reaches the PC
// through a CH34x on UART0. Which one a given board actually presents is not
// something that can be settled from here, so boot messages go to both and
// whichever is listening hears them. An empty console is then evidence about
// the device rather than about the wiring.
//
// Every line is prefixed and stamped with milliseconds since power-on. The
// prefix is there because our output shares one UART with the ESP-IDF's, and
// the first log a tester sent back had "esp_core_dump" and "panel ok" spliced
// into the same word. The timestamp is there because the useful question about
// a boot log is almost never what it says -- it is where it stopped, and how
// long it sat there before it did.
#if ARDUINO_USB_CDC_ON_BOOT
#define TB_OUT(...)              \
  do {                           \
    Serial.printf(__VA_ARGS__);  \
    Serial0.printf(__VA_ARGS__); \
  } while (0)
#else
#define TB_OUT(...) Serial.printf(__VA_ARGS__)
#endif
#define TB_LOG(...)                          \
  do {                                       \
    TB_OUT("[tb %6lu] ", (unsigned long)millis()); \
    TB_OUT(__VA_ARGS__);                     \
  } while (0)

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

// Written, then READ BACK -- but not instantly, and this is the part that was
// wrong for as long as the clock has existed.
//
// The PCF8563 raises VL when its oscillator has stopped, and every read is
// refused while that bit is up. Writing the seconds register clears it, but
// the bit comes straight back up until the crystal is actually running, which
// on a device that has just been flashed or has sat with a flat backup cell
// takes a moment. So the write lands, the immediate read is refused, the
// screen shows nothing -- and after a restart the time is there and correct,
// which is exactly the "reboot to see the time" behaviour this device has
// always had. A second of patience is the whole fix.
bool setClockFromPhone(int64_t localEpochMs) {
  const bool wrote = sensors::setClockFromEpochMs(localEpochMs);
  if (!wrote) {
    Serial.println("clock from phone: the chip refused the write");
    return false;
  }
  sensors::Clock back;
  for (int i = 0; i < 12; i++) {  // ~1.8 s, then we admit it is not starting
    if (sensors::readClock(back)) {
      Serial.printf("clock from phone: set, reads back %02d:%02d after %d ms\n", back.hour,
                    back.minute, i * 150);
      return true;
    }
    delay(150);
  }
  Serial.println("clock from phone: written, but the oscillator has not started");
  return false;
}

// --- auto-rotate ---------------------------------------------------------------
// Only the pinned note follows the accelerometer. Every app screen is laid out
// for a 480x800 portrait canvas and would need reflowing, but a note is just
// text in a box -- it fills whatever shape it is given. A magnet on a fridge
// gets knocked sideways; this is the screen that should not care.
int g_pinnedRotation = 0;

void applyPinnedRotation() {
  // Following the device when that is switched on, and otherwise the angle the
  // note was pinned at. Both go through the same path, so a note that does not
  // follow the device is not a note with no orientation -- it is a note with a
  // chosen one.
  const bool imu = sensors::imuPresent();
  if (!lock::config().autoRotate && g_pinnedRotation == lock::config().pinRotation) return;
  const int want = lock::restRotation(lock::config(), imu, imu ? sensors::orientation() : 0);
  if (want == g_pinnedRotation) return;
  g_pinnedRotation = want;
  epd.setRotation(want);
  touch.setRotation(want);
  epd.clear();
  drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
  epd.displayFull();  // a quarter turn changes every pixel; partial would smear
}

// The angle a sleeping or woken note should be drawn at: the device's, if it is
// set to follow, otherwise the one chosen when it was pinned.
int restRotation() {
  const bool imu = sensors::imuPresent();
  return lock::restRotation(lock::config(), imu, imu ? sensors::orientation() : 0);
}

void showPinned() {
  g_pinnedRotation = restRotation();
  epd.setRotation(g_pinnedRotation);
  touch.setRotation(g_pinnedRotation);
  epd.clear();
  drawPinnedFullScreen(stickyHost.sharedCanvas(), true);
  epd.displayFull();
}

// The grey way down. A picture painted at the four levels the panel really
// has, straight through the grey path -- no canvas, no footer, nothing on it
// but the picture, because a photograph the panel does justice to deserves
// the whole panel. Card art first, matcha's convention (/sleep.bmp, else a
// random face from /.sleep or /sleep), then the grey lock picture a card
// .bmp was turned into. False means nothing grey to show; the 1-bit canvas
// path takes over as it always did.
bool paintGreySleep() {
  uint8_t* gray = (uint8_t*)ps_malloc((size_t)bmpg::OUT_W * bmpg::OUT_H);
  uint8_t* packed = (uint8_t*)ps_malloc(tbg2::BITS);
  bool done = false;
  if (gray && packed) {
    // sleepArtGray borrows the card bus and re-initialises the panel on the
    // way out -- which suits the grey render, whose first pass is a full
    // scrub anyway.
    if (sdcard::sleepArtGray(gray)) {
      bmpg::atkinson(gray, 4);
      bmpg::pack2(gray, packed);
      done = epd.displayGrey2bpp(packed);
    }
    if (!done && lockimg::haveG2()) {
      size_t len = 0;
      char* buf = tfs::readAlloc(lockimg::G2_PATH, len);
      if (buf) {
        if (len == tbg2::FILE_SIZE)
          done = epd.displayGrey2bpp((const uint8_t*)buf + tbg2::HEADER);
        free(buf);
      }
    }
  }
  free(gray);
  free(packed);
  return done;
}

// Light sleep for up to `ms`, ended early by either side button, the power
// button or the touch controller's interrupt line (all active low).
void lightNap(uint32_t ms) {
  static const int kPins[] = {PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_OK, PIN_TP_INT};
  for (const int p : kPins) gpio_wakeup_enable((gpio_num_t)p, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  esp_light_sleep_start();
  for (const int p : kPins) gpio_wakeup_disable((gpio_num_t)p);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
}

// The v2 "Shutting down" banner, shown for a held power button. The
// framebuffer is put back exactly as it was afterwards, because the sleep
// path below may keep the page that was on it.
void announceShutdown() {
  static uint8_t* saved = nullptr;
  if (!saved) saved = (uint8_t*)ps_malloc(EPD_BUF_SIZE);
  if (!saved) return;  // no room: skip the banner, never the shutdown
  memcpy(saved, epd.fb(), EPD_BUF_SIZE);
  stickyHost.fadeBehind();
  chrome2::banner(stickyHost.sharedCanvas(), stickyHost.sharedCanvas().height() / 2,
                  chrome2::BI_POWER, "SHUTTING DOWN", "Your place is saved");
  epd.displayPartial();
  // Long enough to read (owner, 21 Nov: it flashed by too fast to).
  delay(1500);
  memcpy(epd.fb(), saved, EPD_BUF_SIZE);
}

// C6: true while the charging dashboard (or its popup) is on the glass. Its
// frame is never a "page" to keep under the lock screen.
bool g_dashUp = false;
bool usbIn() { return digitalRead(PIN_USB_IN) == HIGH; }

void powerOff(bool lowBattery = false, bool announce = false) {
  // First, while the card and the app are both still there: a reader that
  // has turned a page in the last few seconds has not saved its place yet.
  toybox.flush();
  if (announce && !lowBattery) announceShutdown();
  // E-paper keeps its last image with no power. If a note is pinned, leave that
  // on the panel instead of a goodbye card — that is the whole point of a note
  // you stick on the fridge.
  ToolsCanvas& c = stickyHost.sharedCanvas();
  // Nothing is cleared until it is known whether the frame already on the
  // panel is the one to keep. The framebuffer still holds the last screen
  // drawn, so "keep the page you were reading" costs a checkerboard and a
  // picture rather than a redraw -- but only if the clear does not happen
  // first, which is why this is asked before anything is wiped.
  bool pinned = false;
  char pin[note::NAME_LEN + 1];
  pinned = !lowBattery && note::getPinned(pin);
  const bool keepPage = !g_dashUp && lock::keepsPage(lock::config(), pinned, lowBattery,
                                        toybox.showingOwnPage(), epd.rotation());
  if (keepPage) {
    // The page, knocked back to a grey texture so the picture over it reads as
    // a lock screen rather than as something spilled on the page. Then the
    // picture, whose white is already transparent -- tbimg::draw skips runs of
    // white -- so it lays over the page instead of replacing it.
    // The small cover wants the page well back behind it; the page alone
    // and the cover's ink keep the owner's own setting.
    const uint8_t dimLv = lock::config().empty == lock::EMPTY_COVER_ON_PAGE && lock::config().pageDim < 2
                              ? 2 : lock::config().pageDim;
    epd.dim(dimLv);
    switch (lock::config().empty) {
      case lock::EMPTY_COVER_INK:
      case lock::EMPTY_COVER_ON_PAGE:
        bthumb::drawOverPage(c, lock::config().empty);
        break;
      default:
        lockimg::drawOver(c);
        break;
    }
  } else {
    epd.clear();
    // A pinned note goes down at its resting angle whatever the device was
    // doing a moment ago -- powering off from the middle of a game must not
    // leave the note sideways for the next eight hours. Everything else is
    // portrait.
    const int rot = pinned ? restRotation() : 0;
    epd.setRotation(rot);
    touch.setRotation(rot);
    epd.clear();
  }
  // The one screen that gets the panel's real greys: an unpinned power-off
  // set to show a picture. paintGreySleep paints the panel itself, so the
  // whole canvas path -- footer included -- is skipped.
  const bool paintedGrey = !keepPage && !lowBattery && !pinned &&
                           lock::config().empty == lock::EMPTY_PICTURE && paintGreySleep();
  if (keepPage) {
    // Already done above: the page is on the panel with the picture over it.
  } else if (paintedGrey) {
    // fall through to deepSleep below; the panel already holds the picture
  } else if (lowBattery) {
    // The one case that overrides everything else: if the panel just says
    // "shopping list" the owner has no idea why it stopped responding.
    c.textTrackedCentered(EPD_W / 2, 300, "BATTERY EMPTY", TS_LARGE, true, true, 3);
    c.textCentered(EPD_W / 2, 350, "plug in the USB-C cable to wake it", TS_MED, true);
  } else if (!drawPinnedFullScreen(c)) {
    // Nothing pinned: whatever the lock screen settings asked for.
    switch (lock::config().empty) {
      // Asking for a picture that was never sent falls back to the card rather
      // than to an empty panel that looks like a fault.
      case lock::EMPTY_PICTURE:
        if (lockimg::draw(c)) break;
        // fall through
      // The book you are in the middle of. Copied into flash when the book was
      // opened, so this costs no card and no bus claim on the way to sleep.
      // Asked to keep the page by somebody who was not reading one. The book
      // they were last in is the honest next best thing.
      case lock::EMPTY_PAGE:
      case lock::EMPTY_COVER:
      case lock::EMPTY_COVER_ON_PAGE:  // no page under it: the cover on its own
      case lock::EMPTY_COVER_INK:
        if (tbimg::draw(c, bthumb::LOCK_PATH)) break;
        // fall through
      default:
        c.textTrackedCentered(EPD_W / 2, 200, "GOODBYE!", TS_HUGE, true, true, 4);
        c.textCentered(EPD_W / 2, 260, "press the power button to play again", TS_MED, true);
        break;
    }
  }
  if (!paintedGrey) epd.displayFull();
  epd.deepSleep();
  delay(50);

  // Release the power latch (battery operation powers down here)...
  digitalWrite(PIN_PWR_HOLD, LOW);
  digitalWrite(PIN_PWR_LOCK, LOW);
  delay(100);
  // ...and if USB keeps us alive, deep-sleep with wake on the power button.
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_OK, 0);
  charger::beforeDeepSleep();  // keep charging while asleep on the cable
  esp_deep_sleep_start();
}

// The power button does three things. Held, it always powers off. Released
// quickly it is first offered to the open app as SideBtn::Ok -- only the book
// reader takes it, as its way out of a page -- and otherwise it locks the
// pinned note again, but only from the pinned screen, because a stray press
// in the middle of a game should not end it.
void handlePowerButton() {
  if (digitalRead(PIN_BTN_OK) == LOW) {
    noteActivity();
    if (g_okDownSince == 0) g_okDownSince = millis();
    if (millis() - g_okDownSince > 2000) powerOff(false, true);
    return;
  }
  const bool wasDown = g_okDownSince != 0;
  const uint32_t held = wasDown ? millis() - g_okDownSince : 0;
  g_okDownSince = 0;
  if (wasDown && held > 40) {
    if (toybox.onButton(SideBtn::Ok)) return;
    if (g_pinnedMode) powerOff();
  }
}

// The two side buttons wear two hats.
//
// Inside an app they are edge-triggered presses offered to whatever is open --
// only flashcards wants them -- and deliberately not repeating on hold: these
// grade cards, and a held button that graded ten of them would be a disaster
// you could not undo. 30 ms is longer than any contact bounces for and shorter
// than any deliberate press.
//
// On the home page they are holds, matching the two hint marks drawn level
// with them: hold UP for settings, hold DOWN to carry on with the last app.
// A short press there does nothing at all -- home is a picture, and the only
// touch it answers is the dock. The hold fires while the button is still down
// (there is no release event to wait for), so a fired flag stops it firing
// again until both buttons are up.
//
// The two holds are deliberately different lengths. Carrying on is the thing
// you do twenty times a day and costs nothing if it fires by accident, so it
// comes quickly. Settings is entered rarely and a pocket can press a button
// for a long time, so it wants a deliberate hold -- three seconds, which is
// long past anything a hand does by accident and short enough that it does not
// feel like the device has stopped answering.
constexpr uint32_t SETTINGS_HOLD_MS = 3000;
constexpr uint32_t RESUME_HOLD_MS = 900;

void handleSideButtons() {
  static bool upWas = false, downWas = false;
  static uint32_t upSince = 0, downSince = 0;
  static bool fired = false;
  static uint32_t lastEdge = 0;
  const bool upNow = digitalRead(PIN_BTN_UP) == LOW;
  const bool downNow = digitalRead(PIN_BTN_DOWN) == LOW;

  if (toybox.atHubHome()) {
    if (upNow && !upWas) upSince = millis();
    if (downNow && !downWas) downSince = millis();
    if (!upNow && !downNow) fired = false;
    upWas = upNow;
    downWas = downNow;
    if (fired) return;
    if (upNow && millis() - upSince >= SETTINGS_HOLD_MS) {
      fired = true;
      noteActivity();
      TB_LOG("home: UP held, opening settings\n");
      buzzer::confirm();
      toybox.openSettings();
    } else if (downNow && millis() - downSince >= RESUME_HOLD_MS) {
      fired = true;
      noteActivity();
      // Carry on READING: straight back into the last book at its saved page.
      // With nothing read yet it falls through to reopening the last app.
      //
      // The beep comes FIRST, like the settings hold's does. It used to wait
      // for carryOnReading() to say whether it had worked -- which meant it
      // sounded after a book had been found, opened and painted, seconds
      // after the finger earned it, and on glass that reads as no feedback at
      // all. Asking canCarryOn() first costs one NVS read and lets the sound
      // land on the hold itself.
      if (toybox.canCarryOn()) {
        buzzer::confirm();
        TB_LOG("home: DOWN held, carrying on\n");
        toybox.carryOnReading();
      } else {
        // Nothing to resume. The screen does not change: a low note that says
        // "there isn't one" reads better than a dead button or a detour.
        TB_LOG("home: DOWN held, nothing to carry on with\n");
        buzzer::error();
      }
    }
    return;
  }

  // Both held together for two seconds: one event, once per hold (Focus ends
  // early on it). The single presses that began it still went through above.
  static uint32_t bothSince = 0;
  static bool bothFired = false;
  if (upNow && downNow) {
    if (!bothSince) bothSince = millis() | 1;
    if (!bothFired && millis() - bothSince >= 2000) {
      bothFired = true;
      noteActivity();
      toybox.onBothHeld();
    }
  } else {
    bothSince = 0;
    if (!upNow && !downNow) bothFired = false;
  }
  const bool pressed = (upNow && !upWas) || (downNow && !downWas);
  upWas = upNow;
  downWas = downNow;
  fired = false;
  if (!pressed || millis() - lastEdge < 30) return;
  lastEdge = millis();
  noteActivity();  // a button is a person, whether or not an app wanted it
  toybox.onButton(upNow ? SideBtn::Up : SideBtn::Down);
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

// The v2 low-battery banner: once at 20 % and once at 10 % per discharge,
// over the faded screen, with an OK that any tap answers. Charging back past
// 25 % arms both again.
bool g_bannerUp = false;
int g_batteryWarned = 101;

void showBatteryBanner(int pct) {
  ToolsCanvas& c = stickyHost.sharedCanvas();
  stickyHost.fadeBehind();
  char label[24];
  snprintf(label, sizeof(label), "BATTERY %d%%", pct);
  const int okH = 52;
  const int bottom = chrome2::banner(c, c.height() / 2, chrome2::BI_BATTERY, label,
                                     pct <= 10 ? "Charge me now" : "Charge soon",
                                     "Sync pauses below 10%.", okH + 14, pct);
  c.button(28, bottom - 28 - okH, c.width() - 56, okH, "OK", true);
  epd.displayPartial();
  g_bannerUp = true;
  buzzer::tap();
}

// C6 battery care: with the setting on, charging stops at 80 % and starts
// again below 75 %, through the charger's /CE pin. Only while awake -- a
// powered-off Sticky on the cable is not running to watch, so the sleep path
// re-allows charging (charger::beforeDeepSleep). USB still powers the device
// while charging is held off; only the cell is left alone.
constexpr int CARE_STOP_PCT = 80, CARE_RESUME_PCT = 75;
void handleBatteryCare() {
  static uint32_t last = 0;
  static bool first = true;
  if (!first && millis() - last < 10000) return;
  first = false;
  last = millis();
  const int pct = sensors::batteryPercent();
  bool allow = charger::allowed();
  if (!stickyHost.batteryCare() || pct < 0) allow = true;
  else if (pct >= CARE_STOP_PCT) allow = false;
  else if (pct < CARE_RESUME_PCT) allow = true;
  if (allow != charger::allowed()) {
    charger::allow(allow);
    TB_LOG("battery care: %d%%, charging %s\n", pct, allow ? "on" : "held");
  }
}

void handleLowBattery() {
  if (millis() - g_lastBatteryCheck < 60000) return;
  g_lastBatteryCheck = millis();
  if (sensors::charging()) return;
  const int pct = sensors::batteryPercent();
  if (pct >= 0 && pct <= LOW_BATTERY_PCT) powerOff(true);
  if (pct > 25) g_batteryWarned = 101;
  if (pct < 0 || g_bannerUp || g_pinnedMode) return;
  for (const int at : {10, 20}) {
    if (pct <= at && g_batteryWarned > at) {
      g_batteryWarned = at;
      showBatteryBanner(pct);
      return;
    }
  }
}
}  // namespace

// The Arduino loop task gets 8 KB by default, and everything this firmware
// does on a tap runs inside it: a tool's tick, a cover build, a page of a
// book. Two crashes have now come from that ceiling -- the phone's file list
// (8,448 bytes in one local array) and the sidecar cover builder (a 3,968-byte
// frame) -- and both times the symptom was a silent restart that looked like
// anything but a stack.
//
// The rule stands: nothing over about a kilobyte goes on the stack, and the
// preview harness fails a build that puts it there. This is the belt to that
// pair of braces, and 8 KB of internal RAM is a cheap price for a device that
// restarts instead of telling you why.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  // Power latch FIRST or the board dies when USB is unplugged.
  gpio_hold_dis((gpio_num_t)PIN_PWR_HOLD);
  gpio_hold_dis((gpio_num_t)PIN_PWR_LOCK);
  pinMode(PIN_PWR_HOLD, OUTPUT);
  pinMode(PIN_PWR_LOCK, OUTPUT);
  digitalWrite(PIN_PWR_HOLD, HIGH);
  digitalWrite(PIN_PWR_LOCK, HIGH);
  // Then the charger: until this pin is driven LOW the battery never charges.
  charger::begin();

  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200);
#endif

  // A banner, so a console opened halfway through a session still says what is
  // running. Everything after it is one line per stage, in the order they
  // happen, so the log's last line is always the thing that did not finish.
  delay(50);  // let the ROM's own boot chatter finish before writing over it
  TB_OUT("\n");
#ifdef TB_VERSION
  TB_LOG("Toybox %s, built %s\n", TB_VERSION, TB_DATE);
#else
  TB_LOG("Toybox development build, %s %s\n", __DATE__, __TIME__);
#endif
  TB_LOG("reset reason %d, heap %u B, psram %u B\n", (int)esp_reset_reason(),
         (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getPsramSize());

  // Before anything else that shares the display's bus. epd.begin() does this
  // too, but it runs after the settings are read, and a card left selected
  // through even that much is a card that has been clocked at random.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  // ...and unpowered, not just deselected. The slot sits behind a load switch;
  // held off, an inserted card is glass and cannot touch the shared bus at all.
  // The service screen's probe powers it up for exactly as long as it needs.
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, LOW);

  pinMode(PIN_USB_IN, INPUT);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_OK, INPUT_PULLUP);

  prefs.begin("toybox", false);
  // A build that arrived over Wi-Fi is on trial until it gets as far as its
  // second pass through loop(). This counts the boot and, after three that
  // never got there, goes back to the slot it came from. First thing after
  // NVS, before anything a broken build could crash in.
  otadev::bootGuard(prefs);
  buzzer::begin();
  // Volume if this device has ever been told one, otherwise whatever the
  // on/off switch was set to before volume existed.
  buzzer::setLevel((buzzer::Level)prefs.getInt(
      "sound_lv", prefs.getBool("sound", true) ? (int)buzzer::Level::High : 0));
  TB_LOG("settings loaded, sound level %d\n", (int)buzzer::level());
  TB_LOG("usb sense (GPIO9): %s\n", usbIn() ? "HIGH, cable in" : "LOW, no cable");

  if (!epd.begin()) {
    TB_LOG("EPD alloc failed\n");
    // Nothing can be drawn without a framebuffer, so say it with the one output
    // that does not depend on the panel.
    for (;;) {
      buzzer::error();
      delay(2000);
    }
  }

  // The panel is the one thing that cannot report its own absence, so if it did
  // not answer, say so on the only other output there is.
  //
  // At full volume whatever the sound setting says: this fires only when the
  // screen is dead, and somebody staring at the firmware they just flashed over
  // deserves to be told, even if they had muted it. Three low notes, twice.
  if (!epd.panelAnswered()) {
    TB_LOG("panel: NO ANSWER -- BUSY never moved, display may not be connected\n");
    const buzzer::Level was = buzzer::level();
    buzzer::setLevel(buzzer::Level::High);
    for (int i = 0; i < 6; i++) {
      buzzer::error();
      delay(i == 2 ? 400 : 120);
    }
    buzzer::setLevel(was);
  } else {
    TB_LOG("panel: answered\n");
  }
  // Which of the two controllers this board carries. The SSD2677 replaced the
  // SSD1677 for supply reasons; the driver asks at boot and says so here.
  TB_LOG("panel: controller %s (chip id 0x%02X, busy idle %s)\n", epd.chipName(),
         (unsigned)epd.chipId(), epd.busyIdleHigh() ? "high" : "low");

  // Whatever the service screen last saved about this particular board, before
  // the first pixel is drawn or the first tap is read.
  svc::apply(svc::load());

  // Paint something the instant the panel can paint, before touch, sensors,
  // fonts or buttons are asked for anything. Everything after this point can
  // fail or hang; if it does, this screen is still there saying how far it got,
  // and "the panel works" stops being the last thing you find out.
  {
    ToolsCanvas& c = stickyHost.sharedCanvas();
    epd.clear();
    c.textTrackedCentered(EPD_W / 2, 330, "TOYBOX", TS_HUGE, true, true, 6);
    c.textCentered(EPD_W / 2, 390, "starting", TS_MED, true);
    c.textCentered(EPD_W / 2, 740, "hold a side button for the service screen", TS_SMALL,
                   true);
    epd.displayFull();
    TB_LOG("panel: first paint sent\n");
  }

  touch.begin();
  TB_LOG("touch: %s\n", touch.ok() ? "ok" : "NOT FOUND");
  sensors::begin();
  TB_LOG("sensors: gauge %d rtc %d temp %d tilt %d\n", sensors::batteryPresent() ? 1 : 0,
         sensors::clockPresent() ? 1 : 0, sensors::climatePresent() ? 1 : 0,
         sensors::imuPresent() ? 1 : 0);

  // Full-coverage font packs, if any have been installed (see gfx.h). Loaded
  // before the first paint so a pinned Chinese note wakes up whole.
  TB_LOG("font packs: %d faces\n", gfx::loadFontPacks());
  // The face the owner chose off the card, if there is one and the card is
  // still in. Absent -- no card, no folder, a family deleted since -- the
  // firmware reads in its baked face, which is the same honesty as a device
  // with no card having no books.
  {
    char fam[32] = "";
    stickyHost.prefs().getString("font_uni", fam, sizeof(fam));
    if (fam[0]) TB_LOG("card font: %s %s\n", fam, cardfonts::useUniversal(fam) ? "ok" : "MISSING");
  }

  // Hold UP through power-on to correct the display and touch mapping. This is
  // the one screen that has to work when nothing else does, so it comes before
  // the shell starts and it is driven by buttons alone.
  // Also entered on its own when the touch controller did not answer: the
  // shell is unusable without it, and a device sitting on a hub it will never
  // respond to tells you nothing about why.
  if (svc::requested() || !touch.ok()) {
    TB_LOG("service mode\n");
    svc::run();  // never returns
  }

  // The core draws the pinned footer and the notes portal receives the phone's
  // clock; neither knows what hardware is underneath, so the firmware hands
  // them the two functions that do.
  lock::apply(prefs);
  lock::setInfoHook(fillLockInfo);
  clockset::hook(setClockFromPhone);
  TB_LOG("storage: %s\n", tfs::begin() ? "mounted" : "MOUNT FAILED");

  toybox.begin(stickyHost);
  TB_LOG("apps ready\n");

  // Wait for the button that woke us to come back up, or the first loop would
  // read it as a fresh press and put the device straight back to sleep. Bounded,
  // because this is the last thing before the first real paint and the pin is
  // one of the unverified guesses: a pin that reads low for ever used to mean a
  // blank panel and a device that looked dead.
  const uint32_t releaseWait = millis();
  while (digitalRead(PIN_BTN_OK) == LOW && millis() - releaseWait < 2000) delay(10);
  if (digitalRead(PIN_BTN_OK) == LOW) TB_LOG("warning: OK button reads held at boot\n");
  noteActivity();

  // A device that has just been flashed says so, and asks whether its screen
  // came up the right way round -- the one thing that goes wrong on a fresh
  // board and the one thing a new owner has no way to guess is fixable.
  //
  // Bounded, and any button also dismisses it: this sits between the panel and
  // the hub, and a welcome screen that could not be got past would be a worse
  // bug than the one it exists to catch.
  // Cover verdicts do not survive a firmware change. See bthumb::sweepFailed:
  // several builds condemned covers for running out of heap, which is a fact
  // about the build rather than the book.
  {
    char seen[40] = {};
    if (prefs.isKey("cvsweep")) prefs.getString("cvsweep", seen, sizeof(seen));
    if (strncmp(seen, welcome::version(), sizeof(seen) - 1) != 0) {
      const int gone = bthumb::sweepFailed();
      prefs.putString("cvsweep", welcome::version());
      if (gone) TB_LOG("cover markers cleared: %d\n", gone);
    }
  }

  bool newDevice = false;
  if (welcome::pending(prefs)) {
    const bool updated = prefs.isKey("welcome");
    newDevice = !updated;
    ToolsCanvas& c = stickyHost.sharedCanvas();
    epd.clear();
    welcome::render(c, updated);
    epd.displayFull();
    // Sound as well as picture, because a flash is watched with the panel dark
    // and this is the moment it stops being dark. The two are different on
    // purpose: the long one means this device has nothing stored yet, the
    // short one means it kept everything and only the firmware changed.
    if (updated)
      buzzer::updated();
    else
      buzzer::hello();
    // Said out loud, because this is the one place in boot that stops and waits
    // for a person. Without it the serial log simply ends here, and the first
    // report back was somebody reasonably concluding the firmware had hung.
    TB_LOG("welcome: showing %s, waiting for a tap (up to 120s)\n",
           updated ? "the updated card" : "the first-boot card");
    const uint32_t shown = millis();
    const char* how = "timed out";
    for (;;) {
      TouchEvent ev;
      touch.poll(ev);
      if (ev.tapped) {
        how = "tapped";
        break;
      }
      if (digitalRead(PIN_BTN_UP) == LOW || digitalRead(PIN_BTN_DOWN) == LOW) {
        how = "button";
        break;
      }
      if (millis() - shown > 120000) break;
      delay(20);
    }
    TB_LOG("welcome: dismissed (%s after %lums)\n", how, (unsigned long)(millis() - shown));
    buzzer::tap();
    noteActivity();

    // The tour rides the same once-per-version gate. Each card waits for a tap
    // or a side button, bounded like the welcome was: a card that could not be
    // got past would be a worse bug than an unlabelled icon.
    for (int card = 0; card < tour::CARDS; card++) {
      epd.clear();
      tour::render(c, card);
      epd.displayFull();
      TB_LOG("tour: card %d of %d\n", card + 1, tour::CARDS);
      // Wait for the buttons to come back up first, so the press that
      // dismissed the previous card cannot page through this one too.
      while (digitalRead(PIN_BTN_UP) == LOW || digitalRead(PIN_BTN_DOWN) == LOW) delay(20);
      const uint32_t cardShown = millis();
      bool skip = false;
      for (;;) {
        TouchEvent tev;
        touch.poll(tev);
        if (tev.tapped) break;
        if (digitalRead(PIN_BTN_UP) == LOW || digitalRead(PIN_BTN_DOWN) == LOW) break;
        if (millis() - cardShown > 120000) {
          skip = true;
          break;
        }
        delay(20);
      }
      buzzer::tap();
      noteActivity();
      if (skip) {
        TB_LOG("tour: timed out, skipping the rest\n");
        break;
      }
    }
    welcome::markSeen(prefs);
  }

  // Waking goes to whichever the settings say. With a note pinned the note is
  // usually the thing you came back to, but not everyone agrees, so it asks.
  char pinned[note::NAME_LEN + 1];
  if (newDevice) {
    // A device's very first boot goes through the setup steps (v2); they
    // end at the hub. An updated device skips them -- Settings > DEVICE >
    // Setup runs them again on request.
    TB_LOG("new device: opening setup\n");
    toybox.openSetup(true);
  } else if (note::getPinned(pinned) && lock::config().wake == lock::WAKE_NOTE) {
    TB_LOG("opening the pinned note \"%s\"\n", pinned);
    g_pinnedMode = true;
    showPinned();
  } else {
    TB_LOG("opening the hub\n");
    stickyHost.refresh(true);
  }
  // One note when setup finishes, so a board with a dead panel can still say it
  // got all the way here.
  buzzer::confirm();
  TB_LOG("ready -- %u B heap free\n", (unsigned)ESP.getFreeHeap());
}

// --- C6: the charging dashboard ----------------------------------------------------
// Plugging in while awake (GPIO9 rising) puts up a landscape popup with Cancel;
// three seconds later the dashboard takes the panel: clock, outside weather,
// room, battery. Partial refresh on the minute, orientation checked every
// 10 s, weather every 30 min. Touch does nothing; any button, or pulling the
// cable, powers off with the normal lock screen. Owner, 24-25 Nov.
enum class Dash : uint8_t { Off, Popup, On };
Dash g_dash = Dash::Off;
uint32_t g_dashT0 = 0, g_dashOrientAt = 0, g_dashWxAt = 0, g_dashUnplugAt = 0;
int g_dashPrevRot = 0, g_dashRot = 0, g_dashSecs = -1, g_dashMinute = -1;
bool g_dashWxTried = false;
TRect g_dashCancel{0, 0, 0, 0};
constexpr uint32_t DASH_POPUP_MS = 3000, DASH_ORIENT_MS = 10000, DASH_WX_MS = 30UL * 60 * 1000;

void dashSetRotation(int r) {
  epd.setRotation(r);
  touch.setRotation(r);
}

int dashOrientation() {
  const int o = sensors::imuPresent() ? sensors::orientation() : -1;
  return o < 0 ? 0 : o;
}

void dashGather(chgdash::Info& in) {
  sensors::Clock ck;
  if (sensors::readClock(ck)) {
    in.haveClock = true;
    in.hh = ck.hour;
    in.mm = ck.minute;
    in.year = ck.year;
    in.month = ck.month;
    in.day = ck.day;
  }
  in.pct = sensors::batteryPercent();
  if (!charger::allowed()) in.batt = chgdash::B_HELD;
  else if (sensors::charging()) in.batt = chgdash::B_CHARGING;
  else in.batt = in.pct >= 99 ? chgdash::B_FULL : chgdash::B_USB;
  in.fahrenheit = prefs.getInt("cl_unit", 0) == 1;
  in.noPlace = !wx::haveCity(prefs) || !homenet::haveWifi(prefs);
  in.haveWx = !in.noPlace && wx::lastNow(prefs, in.now);
  wx::Geo g{};
  if (wx::geo(prefs, g) && g.found[0]) snprintf(in.place, sizeof(in.place), "%s", g.found);
  else wx::city(prefs, in.place, sizeof(in.place));
  // Only the town before the first comma: "Bangkok, Thailand" is too long.
  if (char* comma = strchr(in.place, ',')) *comma = 0;
  in.picBorder = stickyHost.chargePicBorder();
  // The picture beside the weather, if the chosen one is in flash.
  if (stickyHost.chargeDash()) {
    switch (stickyHost.chargePic()) {
      case 1:
        if (wallimg::have()) in.pic = wallimg::PATH;
        break;
      case 2:
        if (tbimg::have(lockimg::PATH)) in.pic = lockimg::PATH;
        else if (lockimg::haveG2()) {
          in.pic = lockimg::G2_PATH;
          in.picGrey = true;
        }
        break;
      case 3:
        if (bthumb::haveLock()) in.pic = bthumb::LOCK_PATH;
        break;
      default:
        break;
    }
  }
  int t, rh;
  if (sensors::readClimate(t, rh)) {
    int oi = prefs.getInt("cl_off", clim::OFFSET_DEFAULT);
    if (oi < 0 || oi >= clim::OFFSETS) oi = clim::OFFSET_DEFAULT;
    const int off = clim::OFFSET_DECI[oi];
    in.haveIndoor = true;
    in.inRh = clim::correctedRh(t, rh, off);
    in.inDeci = t + off;
  }
}

void dashDraw(bool full) {
  chgdash::Info in;
  dashGather(in);
  g_dashMinute = in.haveClock ? in.mm : -1;
  chgdash::draw(stickyHost.sharedCanvas(), in);
  if (full) epd.displayFull();
  else epd.displayPartial();
}

void dashStart() {
  g_dash = Dash::Popup;
  g_dashUp = true;
  g_dashT0 = millis();
  g_dashPrevRot = epd.rotation();
  g_dashSecs = 3;
  // Portrait only (owner, 25 Nov). Over the screen that was showing, faded,
  // like every other system banner; an app lying on its side is cleared
  // instead, because its frame would sit sideways under the banner.
  if (g_dashPrevRot == 0) {
    stickyHost.fadeBehind();
  } else {
    dashSetRotation(0);
    epd.clear();
  }
  g_dashCancel = chgdash::drawPopup(stickyHost.sharedCanvas(), g_dashSecs, sensors::batteryPercent());
  epd.displayPartial();
  buzzer::tap();
  TB_LOG("dashboard: plugged in, popup\n");
}

void dashCancel() {
  g_dash = Dash::Off;
  g_dashUp = false;
  dashSetRotation(g_dashPrevRot);
  noteActivity();
  buzzer::tap();
  stickyHost.refresh(true);
  TB_LOG("dashboard: cancelled\n");
}

void dashOpen() {
  g_dash = Dash::On;
  g_dashRot = dashOrientation();
  dashSetRotation(g_dashRot);
  g_dashOrientAt = millis();
  g_dashWxTried = false;
  dashDraw(true);
  TB_LOG("dashboard: open, rotation %d\n", g_dashRot);
}

// Leaving is a power-off, as every button means sleep here. Waits for the
// button to come up first, so a power button still held does not wake the
// chip straight back out of the USB deep sleep.
void dashSleep(const char* why) {
  TB_LOG("dashboard: %s, powering off\n", why);
  const uint32_t t = millis();
  while ((digitalRead(PIN_BTN_OK) == LOW || digitalRead(PIN_BTN_UP) == LOW ||
          digitalRead(PIN_BTN_DOWN) == LOW) &&
         millis() - t < 5000)
    delay(10);
  dashSetRotation(0);
  powerOff();
}

void dashLoop(const TouchEvent& ev) {
  handleBatteryCare();
  const bool anyBtn = digitalRead(PIN_BTN_OK) == LOW || digitalRead(PIN_BTN_UP) == LOW ||
                      digitalRead(PIN_BTN_DOWN) == LOW;
  // Pulled out: half a second of no USB, so a wiggle in the socket is not a
  // power-off.
  if (!usbIn()) {
    if (!g_dashUnplugAt) g_dashUnplugAt = millis() | 1;
    if (millis() - g_dashUnplugAt > 500) {
      if (g_dash == Dash::Popup) dashCancel();  // the popup alone just goes away
      else dashSleep("unplugged");
      g_dashUnplugAt = 0;
      return;
    }
  } else {
    g_dashUnplugAt = 0;
  }
  if (g_dash == Dash::Popup) {
    if (ev.tapped && g_dashCancel.hit(ev.x, ev.y)) {
      dashCancel();
      return;
    }
    const uint32_t el = millis() - g_dashT0;
    if (el >= DASH_POPUP_MS) {
      dashOpen();
      return;
    }
    const int secs = (int)((DASH_POPUP_MS - el + 999) / 1000);
    if (secs != g_dashSecs) {
      g_dashSecs = secs;
      g_dashCancel = chgdash::drawPopup(stickyHost.sharedCanvas(), secs, sensors::batteryPercent());
      epd.displayPartial();
    }
    delay(20);
    return;
  }
  // On.
  if (anyBtn) {
    dashSleep("button");
    return;
  }
  bool redraw = false, full = false;
  if (millis() - g_dashOrientAt >= DASH_ORIENT_MS) {
    g_dashOrientAt = millis();
    const int o = dashOrientation();
    if (o != g_dashRot) {
      g_dashRot = o;
      dashSetRotation(o);
      redraw = full = true;
    }
  }
  sensors::Clock ck;
  if (sensors::readClock(ck) && ck.minute != g_dashMinute) redraw = true;
  // Weather: once soon after opening (the page is up first, from the last
  // reading), then every 30 minutes. One request, Wi-Fi off again after.
  if (wx::haveCity(prefs) && homenet::haveWifi(prefs) &&
      (!g_dashWxTried || millis() - g_dashWxAt >= DASH_WX_MS)) {
    g_dashWxTried = true;
    g_dashWxAt = millis();
    wx::Now n{};
    const wx::Result r = wx::fetch(prefs, n);
    TB_LOG("dashboard: weather %s\n", wx::describe(r));
    if (r == wx::Result::Ok) redraw = true;
  }
  if (redraw) dashDraw(full);
  delay(50);
}

void loop() {
  // setup() finished and one full pass ran: the build works. Not a timer --
  // a device woken for ten seconds and put back to sleep is a working build,
  // and a timer would count that wake as a failure.
  static uint8_t passes = 0;
  if (passes < 2 && ++passes == 2) otadev::confirm(prefs);

  TouchEvent ev;
  // The open app may want a quicker hold than the default (Mines flags on
  // one); asked every pass, since apps come and go between them.
  touch.setHoldMs(toybox.holdMs());
  touch.poll(ev);
  if (ev.tapped || ev.swiped) noteActivity();

  // C6: plugged in while awake -> the dashboard's popup. Edge only: a device
  // that boots on the cable, or is woken on it, does not get one.
  {
    static bool usbWas = usbIn();
    const bool usb = usbIn();
    if (usb != usbWas) TB_LOG("usb: %s\n", usb ? "in" : "out");
    if (g_dash == Dash::Off && usb && !usbWas && stickyHost.chargeDash() && !g_pinnedMode &&
        !g_bannerUp && !toybox.inSettings())
      dashStart();
    usbWas = usb;
  }
  if (g_dash != Dash::Off) {
    dashLoop(ev);
    return;
  }

  if (g_pinnedMode) {
    if (ev.tapped) {
      if (PINNED_HUB.hit(ev.x, ev.y) || PINNED_UNPIN.hit(ev.x, ev.y)) {
        // Unpinning from here rather than from the notes list, because this is
        // the screen you are looking at when you decide you are done with a
        // note. Either way the panel goes back to portrait first: apps are
        // portrait-only, and the hub is what both buttons end up at.
        if (PINNED_UNPIN.hit(ev.x, ev.y)) note::setPinned("");
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

  if (g_bannerUp && (ev.tapped || ev.swiped)) {
    // A banner on the glass takes the next touch for itself: OK, or anywhere.
    g_bannerUp = false;
    buzzer::tap();
    stickyHost.refresh(true);
  } else if (ev.held) {
    noteActivity();
    toybox.onHold(ev.x, ev.y);
  } else if (ev.tapped)
    toybox.onTap(ev.x, ev.y);
  else if (ev.swiped)
    toybox.onSwipe(ev.dx, ev.dy);
  if (toybox.wantsTick()) toybox.tick();

  // The home screen's clock, kept honest with a partial refresh when the
  // minute turns. Only while home is actually showing: apps own their screens,
  // and a sleeping panel is the lock screen's business, which has no clock for
  // exactly this reason. Costs one 0.3 s partial per minute, at most five
  // before idle sleep.
  if (toybox.atHubHome()) {
    static int shownMinute = -1;
    sensors::Clock ck;
    if (sensors::readClock(ck)) {
      if (shownMinute >= 0 && ck.minute != shownMinute) stickyHost.refresh(false);
      shownMinute = ck.minute;
    }
  }

  handleSideButtons();
  handlePowerButton();
  handleLowBattery();
  handleBatteryCare();
  handleIdleSleep();
  // v2: a running timer is the one thing that held the chip fully awake for
  // minutes on end (~20-40 mA). Once nobody has touched it for a few
  // seconds, nap in light sleep instead of spinning: up to a second at a
  // time, woken early by a button or the touch line, with millis() carried
  // across so the countdown stays true. Not on USB (the CDC console drops
  // in light sleep, and there is no battery to save).
  if (toybox.canNap() && !sensors::charging() && millis() - g_lastActivity > 5000 &&
      !g_bannerUp) {
    lightNap(1000);
    return;
  }
  delay(20);
}
