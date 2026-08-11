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
#include <esp_system.h>

#include "board_pins.h"
#include "buzzer.h"
#include "epd.h"
#include "gfx.h"
#include "sensors.h"
#include "service.h"
#include "sticky_host.h"
#include "tools/book_thumbs.h"
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

void powerOff(bool lowBattery = false) {
  epd.clear();
  // E-paper keeps its last image with no power. If a note is pinned, leave that
  // on the panel instead of a goodbye card — that is the whole point of a note
  // you stick on the fridge.
  ToolsCanvas& c = stickyHost.sharedCanvas();
  // A pinned note goes down at its resting angle whatever the device was doing
  // a moment ago -- powering off from the middle of a game must not leave the
  // note sideways for the next eight hours. Everything else is portrait.
  {
    char pin[note::NAME_LEN + 1];
    const bool pinned = !lowBattery && note::getPinned(pin);
    const int rot = pinned ? restRotation() : 0;
    epd.setRotation(rot);
    touch.setRotation(rot);
    epd.clear();
  }
  if (lowBattery) {
    // The one case that overrides everything else: if the panel just says
    // "shopping list" the owner has no idea why it stopped responding.
    c.textTrackedCentered(EPD_W / 2, 300, "BATTERY EMPTY", TS_LARGE, true, true, 3);
    c.textCentered(EPD_W / 2, 350, "plug in the USB-C cable to wake it", TS_MED, true);
  } else if (!drawPinnedFullScreen(c)) {
    // Nothing pinned: whatever the lock screen settings asked for.
    switch (lock::config().empty) {
      case lock::EMPTY_BLANK: break;  // a device that looks off, because it is
      // Asking for a picture that was never sent falls back to the card rather
      // than to an empty panel that looks like a fault.
      case lock::EMPTY_PICTURE:
        if (lockimg::draw(c)) break;
        // fall through
      // The book you are in the middle of. Copied into flash when the book was
      // opened, so this costs no card and no bus claim on the way to sleep.
      case lock::EMPTY_COVER:
        if (tbimg::draw(c, bthumb::LOCK_PATH)) break;
        // fall through
      default:
        c.textTrackedCentered(EPD_W / 2, 200, "GOODBYE!", TS_HUGE, true, true, 4);
        c.textCentered(EPD_W / 2, 260, "press the power button to play again", TS_MED, true);
        break;
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

// The power button does three things. Held, it always powers off. Released
// quickly it is first offered to the open app as SideBtn::Ok -- only the book
// reader takes it, as its way out of a page -- and otherwise it locks the
// pinned note again, but only from the pinned screen, because a stray press
// in the middle of a game should not end it.
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
// for a long time, so it wants five deliberate seconds.
constexpr uint32_t SETTINGS_HOLD_MS = 5000;
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
      if (toybox.carryOnReading()) {
        TB_LOG("home: DOWN held, carrying on\n");
        buzzer::confirm();
      } else {
        // Nothing to resume. The screen does not change: a low note that says
        // "there isn't one" reads better than a dead button or a detour.
        TB_LOG("home: DOWN held, nothing to carry on with\n");
        buzzer::error();
      }
    }
    return;
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

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_OK, INPUT_PULLUP);

  prefs.begin("toybox", false);
  buzzer::begin();
  // Volume if this device has ever been told one, otherwise whatever the
  // on/off switch was set to before volume existed.
  buzzer::setLevel((buzzer::Level)prefs.getInt(
      "sound_lv", prefs.getBool("sound", true) ? (int)buzzer::Level::High : 0));
  TB_LOG("settings loaded, sound level %d\n", (int)buzzer::level());

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
  nweb::setClockHook(setClockFromPhone);
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
  if (welcome::pending(prefs)) {
    const bool updated = prefs.isKey("welcome");
    ToolsCanvas& c = stickyHost.sharedCanvas();
    epd.clear();
    welcome::render(c, updated);
    epd.displayFull();
    buzzer::confirm();
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
  if (note::getPinned(pinned) && lock::config().wake == lock::WAKE_NOTE) {
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

void loop() {
  TouchEvent ev;
  touch.poll(ev);
  if (ev.tapped || ev.swiped) noteActivity();

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

  if (ev.tapped)
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
  handleIdleSleep();
  delay(20);
}
