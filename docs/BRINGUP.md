# First boot checklist

This firmware has not run on a physical device yet. Every screen is verified
on a PC, but some hardware details (pin assignments, display orientation,
touch mapping) come from vendor demo code and community notes rather than
from testing on real hardware. Go through this list in order the first time
you flash a device. Early items need to work before later ones can be tested.

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
- Mirrored or upside-down image: adjust the scan direction bits in
  `epd.cpp` (command 0x01). Fix the display before touching anything else,
  because touch is mapped to match the display.
- Light ghosting after partial refreshes is normal. The firmware does a
  full refresh every 40 partial ones.

## 3. Touch

Tap every tile on the hub and check the right app opens.

- Taps land on the wrong tile: the touch orientation mapping in `touch.cpp`
  is wrong for this panel. With the display confirmed correct, flip one
  axis at a time.
- `touch: NOT FOUND`: the GT911 controller has two possible addresses
  (0x5D and 0x14) and a power enable on GPIO42. Try the alternate address.

## 4. Sound, buttons, battery

- Taps should click when sound is on.
- Holding the power button for two seconds should show the goodbye screen
  and power off. A short press should wake it again.
- If the device goes back to sleep immediately after waking, the button
  polarity is likely wrong (OK button, GPIO4, active low).

## 5. Storage

- The first boot formats the filesystem. This takes a few seconds, once.
- The notes and flashcards apps create sample content. Check both open.
- Reboot and confirm stats and content survive.
- If font packs are installed in `/fonts/`, the boot log should count
  3 faces per pack, and rare characters should render instead of showing
  boxes.

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
