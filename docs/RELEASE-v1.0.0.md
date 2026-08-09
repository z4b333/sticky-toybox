# Toybox v1.0.0

Paste-ready notes for the GitHub release page.

---

**v0.1 was written for a device I had never held. v1.0.0 is the same firmware
after the device arrived.**

Everything below the line existed at v0.1 in some form. What changed is that
the parts touching hardware stopped being guesses — and six of them turned out
to be wrong, each presenting as a different problem from what it was.

## Install

Flash it from Chrome or Edge at
**[z4b333.github.io/sticky-toybox](https://z4b333.github.io/sticky-toybox/)**,
or write `toybox-full.bin` to offset 0 with esptool. The page now tells you
which build it is about to write, so you can check afterwards that it landed.

The first time you run it, hold either side button while it powers on. That
reaches the service screen, which reports what the board answered on each bus
and can correct the display and touch orientation if your unit disagrees with
mine.

## What bring-up fixed

**It now boots.** The flash mode must be `dio`, not `qio` — this module is an
ESP32-S3**R8**, whose octal PSRAM shares the extra SPI data lines, so a header
asking for quad reads makes the ROM load the bootloader as garbage. The symptom
was a watchdog loop and a blank panel, which looks exactly like a failed flash.

**The image is no longer repeated over noise.** The display controller was
being told the canvas's dimensions rather than the panel's. Both are 800 and
480, in different orders.

**Both panel flips default on.** That is this panel's scan direction, not a
quirk of one unit, so a new device comes up the right way round instead of
making its owner discover the service screen.

**Landscape turns the right way.** The two landscape rotations were transposed,
so a pinned note turned a half turn from where it should. Portrait was
unaffected, which is why it survived so long.

**Leaving a pairing screen no longer crashes.** The captive portal's DNS server
owns a UDP socket whose destructor touches the network stack; it was being
destroyed after WiFi came down.

**The pairing QR code can actually be scanned.** The vendored QR encoder never
checked that a payload fitted the version it was given — it padded by
subtracting the length from the capacity, underflowed, and wrote through the
error-correction region. It reported success and drew a symbol no camera could
read. The WiFi code was 37 bytes in a version that holds 26.

## What using it changed

**The text is bigger.** Every size moved up one step, to 16/24/32/44 px. The
old scale was chosen by counting pixels on a monitor; at 235 DPI that put body
text at 1.7 mm, about five point. It is 2.6 mm now.

**Thai renders at the right size.** It is set one step larger than it is asked
for, because a Thai line reserves two storeys of marks above the letters and
one below, leaving them about half the height Latin gets in the same box.

**The list picker keeps its own script.** It was folding every non-ASCII
character to a question mark, under a rule left over from a font that had not
been in the firmware for a long time. A Thai list arrived as rows of `??????`.

**The side buttons grade flashcards.** DOWN reveals the answer and then takes
the card as known, UP sends it back to be asked again — so a twenty-minute
session does not mean reaching up to the panel for every card.

**Beep volume is high, medium, low or mute**, rather than on or off.

**The lock screen has no clock.** It had one, and it was the default, until the
obvious was pointed out: a panel that holds its last image with no power is
exactly the wrong place for a clock. The time was drawn on the way to sleep and
stayed wrong for hours. A goodbye card, a picture, or a blank panel are the
choices now.

**A picture can be the lock screen.** Sent from the notes page on your phone,
where the browser crops, greyscales and dithers it and shows you the result
before it goes — a photograph at one bit per pixel is either striking or mud,
and there is no way to tell but to look.

**The lock screen has settings of its own**: sleep timing, what an empty panel
shows, where the power button wakes to, and whether the note follows the
accelerometer.

## Known limitations

- The board's 8 MB of PSRAM is not configured, so the service screen reports
  none. Nothing currently needs it.
- The microSD slot is unused. It shares the display's SPI bus and has never
  been exercised.
- Thai line breaking is at character-cluster level, not word level.
- Characters above U+FFFF, including emoji, are not supported.

## Verified on hardware

Touch, all four sensors, the buzzer, both side buttons, WiFi pairing for notes,
flashcards and the picker, the four display rotations, and a full charge-to-use
session. The accelerometer mapping was confirmed against four gravity readings
rather than assumed.

Not verified: microSD, and running down the battery to the low-battery
shutdown.
