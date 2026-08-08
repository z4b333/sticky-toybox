# First boot checklist

This firmware has not run on a physical device yet. Every screen is verified
on a PC, but some hardware details (pin assignments, display orientation,
touch mapping) come from vendor demo code and community notes rather than
from testing on real hardware. Go through this list in order the first time
you flash a device. Early items need to work before later ones can be tested.

## The service screen

Hold either of the two side buttons (UP or DOWN) while the device powers on.
It also opens by itself if the touch controller does not answer, since the
rest of the firmware is unusable in that case.

This screen is driven entirely by the three physical buttons, so it works
with touch completely broken. It shows what answered on each bus at boot,
and it can correct the two guesses most likely to be wrong:

- **SCREEN MIRRORED LEFT/RIGHT** and **SCREEN UPSIDE DOWN** fix the panel.
  Between them they reach every way the scan direction can come out wrong.
- **TOUCH: SWAP X AND Y**, **FLIP LEFT/RIGHT** and **FLIP UP/DOWN** fix the
  digitizer. These three reach all eight orientations it can be in.
- **TOUCH TEST** draws a cross wherever you touch. If the cross lands under
  your finger in all four corners, the mapping is right.

UP and DOWN move the highlighted row, OK changes it, and holding OK saves
and restarts. What you save is applied on every boot from then on, so
nothing has to be rebuilt or reflashed.

Fix the screen before the touch: the touch correction is applied on top of
the panel one, so it only makes sense once you can see what you are aiming
at.

## 0. Before flashing

- Work on USB power for the first session.
- Flash with `pio run -t upload`, or use the web flasher.
- If no serial port appears: the Sticky uses a CH34x USB-serial chip, not
  native USB. Windows may need the CH343 driver from wch-ic.com.

## 1. Boot

Open a serial monitor at 115200. You should see the touch probe result
(`touch: ok`), a `font packs:` line, and no repeated resets.

- Resets when USB is unplugged: the power latch pins (GPIO45/46) may be
  wrong for this board revision. They are driven high at the top of
  `setup()`.
- `EPD alloc failed`: the framebuffer allocation failed. Check that PSRAM
  is detected.

## 2. Display

The hub should appear within about two seconds.

- Blank screen: check the SPI pins and the panel power enable (GPIO47).
  Note the display shares its SPI bus with the SD card slot. If the display
  works until the SD card is used, lower the display clock to 10 MHz (see
  the note in `platformio.ini`).
- Mirrored or upside-down image: fix it from the service screen above, not
  in the source. If you would rather change the default for every device,
  the scan direction bits are in `epd.cpp` (command 0x01).
- Light ghosting after partial refreshes is normal. The firmware does a
  full refresh every 40 partial ones.

## 3. Touch

Tap every tile on the hub and check the right app opens.

- Taps land on the wrong tile: use TOUCH TEST on the service screen. With
  the display already confirmed correct, change one of the three touch rows
  at a time and watch where the cross lands. The defaults there are the
  values in `touch.cpp`, so once you find the right combination you can make
  it the built-in default if you want to.
- `touch: NOT FOUND`: the GT911 controller has two possible addresses
  (0x5D and 0x14) and a power enable on GPIO42. Try the alternate address.

## 4. Sound, buttons, sleep

- Taps should click when sound is on.
- Holding the power button for two seconds should show the goodbye screen
  and power off. A short press should wake it again.
- If the device goes back to sleep immediately after waking, the button
  polarity is likely wrong (OK button, GPIO4, active low).
- Leave it untouched for five minutes. It should go to sleep on its own,
  keeping whatever was on the screen. A running timer correctly prevents
  this, so test with the hub showing.

## 5b. Sensors

The four chips on the sensor bus (fuel gauge 0x55, RTC 0x51, temperature
0x44, accelerometer 0x6A) are probed at boot. The serial log prints which
answered:

```
sensors: gauge 1 rtc 1 sht 1 imu 1
```

A zero means that chip did not respond, and its feature turns itself off.
Nothing hangs; the device works without any of them.

- **Gauge 0**: no battery icon on the hub, and no low-battery shutdown.
  Check the bus wiring before assuming the chip is missing.
- **Battery icon reads full while unplugged, or the charging bolt is
  backwards**: the CHARGE_STATE polarity (GPIO40) is a guess. Flip the
  comparison in `sensors.cpp`.
- **RTC 0 or the clock shows nothing**: the clock is only set when you save
  a note from your phone. Write one note, then check the pinned screen
  footer.
- **IMU 0, or the note rotates the wrong way**: the accelerometer axis to
  screen rotation mapping in `sensors.cpp` is unverified. Turn the device
  slowly through all four positions and note which way it goes, then adjust
  the comparisons in `orientation()`.

## 5. Storage

- The first boot formats the filesystem. This takes a few seconds, once.
- The notes and flashcards apps create sample content. Check both open.
- Reboot and confirm stats and content survive.
- If font packs were selected in the web flasher (or copied to `/fonts/`),
  the boot log should count 3 faces per pack, and rare characters should
  render instead of showing boxes.

## 6. Wireless (needs a phone or second device)

- Notes, flashcards and picker: tap WRITE / IMPORT / FROM PHONE, join the
  WiFi QR code with a phone, and check the page opens and content arrives.
- Battleship two-player mode needs a second device.

## 7. Judgement calls that need a real screen

Some choices were made from rendered previews and can only be confirmed on
actual e-paper:

- Is Korean readable at 16 px, or should it move to 24 px like Thai?
  (One-line change in `scriptFloor`.)
- Does the 2048 merge blink feel right with real refresh timing?
- Is the missing-character box visible enough at arm's length?

Anything that looks wrong can be reproduced on a PC with the preview
harness in `test/host/`, fixed there, and compared before reflashing.
