# First boot on real hardware

Nothing in this repository has run on a physical reTerminal Sticky yet. Every
screen is host-verified pixel by pixel, but several hardware facts are taken
from vendor demo code and community bring-up notes rather than measured on
this project's own device. This is the checklist for the day the hardware
arrives — in order, because the early items decide whether the later ones can
be tested at all.

## 0. Before flashing

* Charge, or work on USB power. The power latch (step 2) behaves differently
  on battery, and a brown-out mid-flash is confusing on day one.
* `pio run -t upload` with the device in bootloader mode if needed. If the
  serial port never appears: the Sticky routes its console through an
  on-board WCH USB-serial bridge on UART0 (not native USB CDC) — install the
  CH34x driver if your OS lacks it.

## 1. It boots and says so

`pio device monitor -b 115200`. Expect, in order: the touch probe result
(`touch: ok` / `NOT FOUND`), `font packs: N faces`, and no reset loop.

* **Reset loop before any output** → power latch pins. `PIN_PWR_HOLD` (45)
  and `PIN_PWR_LOCK` (46) are driven HIGH first thing in `setup()`; if the
  board still dies when USB is unplugged, those assignments are wrong for
  this board revision.
* **`EPD alloc failed`** → framebuffer allocation; check PSRAM came up
  (`esptool flash_id` should report the S3R8's octal PSRAM).

## 2. Panel

The hub should draw within ~2 s of boot.

* **Blank panel** → SPI pins or the EP_PWR_EN rail (GPIO47). The display
  shares its SPI bus with the SD slot (SCK13 / MOSI14, panel CS15, SD CS8) —
  if the panel works until SD is touched, the bus sharing is the suspect;
  pin the display clock to 10 MHz (see `platformio.ini` notes).
* **Mirrored or upside-down image** → scan direction: the TB bits of command
  0x01 in `epd.cpp`. Fix the panel FIRST, then touch — touch is mapped to
  match the panel, so fixing them in the wrong order chases a moving target.
* **Ghosting on partial refresh** → expected in small amounts; the firmware
  forces a full refresh every 40 partials. If it is severe, the panel needs
  the vendor LUT rather than the driver default.

## 3. Touch

On the hub, tap each of the 13 tiles and confirm the right app opens.

* **All taps land, but on the wrong tile** → orientation mapping. The GT911
  is configured swapXY + flip both (`touch.cpp`); with the panel confirmed
  correct in step 2, flip one axis at a time.
* **`touch: NOT FOUND`** → the GT911 has two addresses (0x5D, alt 0x14) and
  a power-enable on GPIO42; the reset/INT dance in `touch.cpp` selects the
  address. Scope or retry with the alternate address.
* **Taps drift from targets by a constant amount** → the panel's GT911
  reports coordinates at byte 0 without a track-id byte (as configured);
  if this board revision differs, offsets appear — flip
  `gt911CoordsAtByte0` behavior in the driver.

## 4. Sound, buttons, battery

* Any tap should click (if sound is on in Settings). Silence → LEDC buzzer
  on GPIO48.
* Hold the power button 2 s from the hub → goodbye screen, then power-off.
  Short-press wake should return to the hub (or the pinned note, once one
  exists). If the device sleeps instantly on wake, the wake-button release
  wait in `setup()` isn't seeing the button go up — check the OK button
  polarity (active-low, GPIO4).
* UP (5) / DOWN (6) currently unused by the firmware; verify they read at
  all for future one-handed navigation.

## 5. Filesystem and persistence

* First boot formats the 11.9 MB LittleFS partition (takes a few seconds,
  once). The notes tool creates its sample note; the flashcards tool its
  sample deck. Both should list without errors.
* Reboot: Wordle stats, coin tally and settings must survive (NVS), and the
  sample content must still be there (LittleFS).
* If packs are installed in `/fonts/`, the boot log's `font packs: N faces`
  should read 3 per installed pack, and a rare character (뷁 for the Korean
  pack) should render as a glyph, not a hollow box.

## 6. Radio features (later, needs a second device / phone)

* Notes/flashcards/picker pairing: tap WRITE / IMPORT / FROM PHONE, join the
  QR wifi from a phone, confirm the page opens by itself and content arrives.
* Battleship two-device duel over ESP-NOW: needs the second Sticky.

## 7. The judgement calls that need eyes on glass

These are decisions made on rendered previews that only real e-paper can
confirm:

* hangul at 16 px — readable, or should its floor move to 24 px like Thai's
  (one-line change in `scriptFloor`)?
* partial-refresh feel in 2048's merge blink and the timer's second-hand
  stepping;
* the missing-glyph hollow box at arm's length;
* dark-on-light contrast of the dithered progress bars.

Whatever differs from expectation: every screen here can be reproduced
host-side (`test/host/`), so fix, re-render, compare, then re-flash.
