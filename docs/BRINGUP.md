# First boot

This firmware has never run on a physical device. Every screen has been drawn
and checked on a PC, and the game logic is tested, but the parts that touch
hardware — pin numbers, display scan direction, touch orientation, the power
latch — come from the vendor's demo code, the V01 schematic and community
notes. They are educated guesses. Some of them will be wrong.

This page is the order to do things in, and what to do when something does not
work. Work through it top to bottom. Each step assumes the ones before it
passed, because most of them cannot be judged otherwise: you cannot tell
whether touch is mapped correctly until you trust what is on the screen.

Set aside an hour. Nothing here is difficult, but rushing past a failed step
makes the next three impossible to interpret.

There is a tickable version of all this at
[checklist.html](https://z4b333.github.io/sticky-toybox/checklist.html), meant
for a phone held next to the device. It produces a report you can paste back
when something needs looking at. It keeps nothing between page loads, so copy
the report out before you close it.

## What you need

A USB-C cable that carries data. A charge-only cable will power the device and
never show a serial port, which looks exactly like a dead board. If you have
any doubt, try a different cable before you conclude anything.

Chrome or Edge, if you want to flash from the browser. Firefox and Safari do
not support Web Serial and the install button will not appear at all.

On Windows, the CH343 driver from wch-ic.com. The Sticky talks to your PC
through a CH34x USB-serial chip rather than the ESP32's own USB, so without
that driver no COM port appears no matter how healthy the device is. Install it
before you plug anything in. macOS and Linux need nothing.

Keep the device on USB power for this whole session. Battery operation depends
on the power latch, which is one of the guesses, and you do not want to be
diagnosing a display problem and a power problem at the same time.

## Step 0 — power it on before you flash anything

Press the power button and watch the screen with the firmware it shipped with.

This takes thirty seconds and it is the only chance you get to see the device
working before you change it. Note what the vendor firmware looks like: which
way up the image is, whether touch responds, whether the buttons do anything.
If Toybox later comes up mirrored you will know whether that is my mistake or
the panel's nature. If the device does nothing at all now, the problem is not
software and nothing below will help.

Then plug it into your PC and confirm a serial port appears. On Windows look in
Device Manager under Ports for a USB-SERIAL CH34x entry. On macOS or Linux run
`ls /dev/tty.*` or `ls /dev/ttyUSB* /dev/ttyACM*`. If nothing shows up, stop
here — it is the cable or the driver, and flashing cannot work until that is
sorted.

## Step 1 — flash Toybox

Two ways, pick either.

**From the browser.** Open https://z4b333.github.io/sticky-toybox/ in Chrome or
Edge, tick any language packs you want, press install, and choose the serial
port. If that page is not published yet you can serve it from the repo instead:
run `python -m http.server 8000` inside the `docs` folder and open
`http://localhost:8000`. Web Serial trusts localhost the same way it trusts
HTTPS, so this behaves identically.

**From the command line.** `pip install esptool`, then:

```
esptool --chip esp32s3 write_flash 0x0 docs/firmware/toybox-full.bin
```

That single file contains the bootloader, the partition table and the app. If
you also want language packs, they go to fixed addresses:

```
esptool --chip esp32s3 write_flash 0x410000 docs/firmware/zh_full.tfp
esptool --chip esp32s3 write_flash 0x840000 docs/firmware/ko_full.tfp
esptool --chip esp32s3 write_flash 0xa90000 docs/firmware/ja_full.tfp
```

You do not have to decide now. Packs can be added or removed later without
touching the firmware, and everything except rare Chinese, Korean and Japanese
characters already works without them.

If flashing never starts, the board is probably not entering download mode by
itself. Most ESP32 boards are put there by the serial adapter toggling DTR and
RTS, and whether this one does is not something I can confirm from here. If
esptool sits at "Connecting..." and gives up, that is the thing to look into —
check Seeed's own flashing notes for the Sticky.

## Step 1b — what a working boot looks like

Every line the firmware prints is prefixed `[tb` and stamped with milliseconds
since power-on, so it can be told apart from the ESP-IDF's own output on the
same UART. A healthy first boot looks about like this:

```
[tb     52] Toybox v1.0.0-beta.3, built 10 Aug 2026
[tb     53] reset reason 1, heap 298000 B, psram 0 B
[tb     55] settings loaded, sound level 3
[tb    210] panel: answered
[tb   1980] panel: first paint sent
[tb   2010] touch: ok
[tb   2050] sensors: gauge 1 rtc 1 temp 1 tilt 1
[tb   2060] font packs: 0 faces
[tb   2210] storage: mounted
[tb   2260] apps ready
[tb   2280] welcome: showing the first-boot card, waiting for a tap (up to 120s)
[tb   9120] welcome: dismissed (tapped after 6840ms)
[tb  10900] opening the hub
[tb  10900] ready -- 240000 B heap free
```

The last line printed is the stage that did not finish, which is the only
question a boot log usually has to answer. A log that stops at `welcome:
showing` is not stuck — it is waiting for you.

The panel paints **TOYBOX / starting** within about two seconds of power-on,
before touch, the sensors, the fonts or the buttons are asked for anything.
Then it goes to the hub, or to the pinned note, and the buzzer gives one short
note when it is fully up.

That screen exists to split "nothing happened" into two very different
problems. If you see it, the framebuffer, the SPI bus, the panel rail and the
display driver are all working, and whatever went wrong went wrong later. If
you do not see it, none of that is proven yet.

A blank white panel is indistinguishable from a device that is switched off, so
press the power button once before concluding anything.

## Step 2 — go to the service screen first, not the hub

**Hold either side button (UP or DOWN) while the device powers on, and keep
holding until something appears.** The button is read a second or two in, after
the panel and the fonts are up.

You will get a screen headed SERVICE. It is driven entirely by the three
physical buttons, so it works even with touch completely broken, and it opens
by itself if the touch controller does not answer.

Do this before you look at anything else. It is the one screen that tells you
what the board actually is, and the only one that can correct the two guesses
most likely to be wrong.

Read the top block:

```
panel    answered
touch    found at 0x5D
sensors  gauge 1   clock 1   temp 1   tilt 1
battery  3.98 V
psram    8192 KB
fonts    0 extra faces
```

`panel NO ANSWER` means the display never drove its BUSY line, which is how a
device that boots, logs, sleeps and beeps perfectly can still be showing the
firmware you flashed over — e-paper holds its last image for ever, so nothing
about the screen tells you the screen is not being written to. The firmware
also plays six low notes at boot in that case, at full volume whatever the
sound setting says, because the one output that cannot report a dead display is
the display. Check the ribbon seating and try a full power-off rather than a
reset: the previous firmware can leave the controller in deep sleep.

**With a card in the slot, the device may not boot at all.** This is known and
not yet explained. The card shares the display's SPI bus, and its chip select
is now driven high from the first instruction of setup() so the card can never
be selected while the panel is being written to — but on the one board this has
been tried on, a 128 GB card still stopped it starting, and inserting one while
running produced a screen full of glitches. Take the card out and it boots
normally. Nothing on the device needs a card.

If you can, capture the serial log with a card in. It answers the question that
matters: a log that runs to `ready` means the firmware is fine and only the
panel is affected, and a log with nothing in it at all means the card is
browning out the board at power-up, which is a different problem with a
different fix.

**TEST THE SD CARD** is the one row that does something rather than setting
something. Nothing in the firmware uses a card yet; the row exists because the
card shares the display's SPI bus, and whether those two can coexist decides
whether this device can ever read books — a 480x800 page is 48 KB and internal
flash holds about a hundred of them.

Put any card in, select the row and press OK. It mounts, counts the root,
times a 48 KB read, and then resets the display controller and asks whether it
still answers. That last step is the point: two devices on one bus fail in a
way where the card reads perfectly and the panel quietly stops updating, and
"the SD works" is a different claim from "the SD works and you keep your
screen".

`touch NOT FOUND` means the GT911 did not answer at either address. Everything
else can still be checked from this screen with the buttons, but the device is
not usable until that is sorted — see the troubleshooting list at the end.

A `0` in the sensors line means that chip did not respond, and its feature
turns itself off quietly. Nothing hangs. The device works fine without any of
them; you lose the battery icon, the clock on the pinned note, the temperature,
or the auto-rotate, in that order.

`psram 0 KB` would be serious — the framebuffer lives there — but in that case
you would have seen `EPD alloc failed` on the serial log and a blank screen
rather than this one.

## Step 3 — fix the screen

The SERVICE title is white text on a solid black bar. That bar is drawn across
the **top** of the panel, and the list of rows runs down from it.

If the black bar is at the bottom and the list runs upward, the panel is
scanning the other way: select **SCREEN UPSIDE DOWN** and press OK to set it to
YES.

If the text reads as a mirror image — legible only in a mirror, with the bar
still at the top — set **SCREEN MIRRORED LEFT/RIGHT** to YES.

Both can be true at once. The screen repaints fully after each change so you
see the result immediately; if it looks worse, press OK again to undo it.
Between these two settings you can reach every orientation the panel can come
out in, so keep going until the title is at the top and reads normally.

Do not move on until this is right. The touch correction is applied on top of
the display one, and there is no way to judge where a tap landed if you cannot
trust what you are looking at.

## Step 4 — fix the touch

Move down to **TOUCH TEST**. Touch the screen. A cross is drawn where the
firmware thinks your finger was, and the coordinates print at the bottom.

If the cross lands under your finger in all four corners and in the middle,
touch is already mapped correctly and you can skip the rest of this step. Check
the corners specifically — an inverted axis looks fine in the centre of the
screen and is obviously wrong at the edges.

Otherwise work in this order, because the swap changes what the flips mean.

Drag your finger slowly downward. If the cross travels sideways instead, set
**TOUCH: SWAP X AND Y**, then come back to the test.

Now touch near the left edge. If the cross appears near the right edge, set
**TOUCH: FLIP LEFT/RIGHT**.

Then touch near the top. If the cross appears near the bottom, set **TOUCH:
FLIP UP/DOWN**.

There are eight combinations of those three settings and one of them is right,
so this always terminates. Check the four corners again once you think you have
it.

## Step 5 — save

Hold OK for about two seconds. The screen says SAVED and the device restarts.

What you saved is written to NVS and applied on every boot from then on, before
the first pixel is drawn. It survives reflashing the firmware, and the settings
screen's reset does not touch it. If you ever want to start over, the service
screen is always there.

You should now be looking at the hub: TOYBOX at the top, thirteen tiles in
three groups, a gear in the top-right corner.

## Step 6 — the hub and the apps

Tap each tile and check the app you expected opens. Taps should click if sound
is on. The way back is the chevron and the word HUB in the top-left corner; its
touch area reaches ten pixels below the bar it is drawn in, so aim generously.

Open the gear. Hide an app, go back, and confirm the hub reflows without it.
Turn it back on.

Play a full game of something — XO is quickest — and confirm the record at the
bottom updates.

Ghosting after a few partial refreshes is normal for e-paper. The firmware
forces a full refresh every 40 partial ones, which is what clears it.

## Step 7 — power, buttons and sleep

Hold the power button for two seconds. The device should draw its lock screen
and switch off, leaving that image on the panel — e-paper keeps its last frame
with no power at all. Which image that is comes from settings: a pinned note if
you have one, otherwise a goodbye card, and a picture or a blank panel are the
other choices. A short press should bring it back.

If it goes straight back to sleep after waking, the button polarity is wrong
(OK is GPIO4, expected active low).

Leave it untouched for five minutes with the hub showing. It should put itself
to sleep. Then start a countdown timer and leave it again: a running timer is
supposed to hold the device awake, and that is the more interesting half of the
test.

**Now unplug the USB cable.** This is the power latch test. GPIO45 and GPIO46
are driven high at the very top of `setup()` to hold the board alive on
battery, and those pin numbers started as a guess taken from the vendor demo.

**They were right.** The owner's board has run on battery from the first build
and has never dropped when the cable came out, across every release since. This
step is kept because a new board is a new board, but it is no longer one of the
things anyone is waiting to find out.

## Step 8 — the phone side

Open Notes, tap WRITE, and scan the QR code with your phone's camera. It joins
the device's own access point and opens an editor page. Type something, save,
and check it appears on the device. Tick a checkbox on the device and confirm
it stays ticked.

The clock is set from your phone the first time you save a note, because the
device has no network time. After that, pin the note and check the footer shows
the time and the room temperature once the device sleeps.

Flashcards and the list picker work the same way through the same pairing.

Battleship's two-device mode needs a second Sticky, so it will have to wait.

## Step 9 — the things only a real screen can settle

These were judged from rendered previews and cannot be decided on a PC. Have a
look and tell me what you think.

Is Korean readable at 16 px, or should it move up to 24 like Thai? That is a
one-line change.

Is the hollow box drawn for a missing character visible at arm's length?

Do the dice look right at 200 px for a single die, or absurd?

## If something is wrong

**No serial port.** The cable (it must carry data) or the driver (CH343 on
Windows). Not the board.

**Blank screen after flashing.** Check the panel power rail (GPIO47) and the
SPI pins. The display shares its SPI bus with the SD card slot; if it works
until the SD card is used, drop the display clock to 10 MHz — there is a note
in `platformio.ini`.

**`EPD alloc failed` on serial.** PSRAM was not detected. That is a build or
board configuration problem rather than a wiring one.

**Repeated resets, or it dies when USB is unplugged.** The power latch pins
(GPIO45/46).

**Mirrored or upside-down image.** Service screen, step 3. Do not edit the
source — the correction is saved per device.

**Taps land in the wrong place.** Service screen, step 4.

**`touch: NOT FOUND`.** The GT911 has two possible addresses (0x5D and 0x14),
both of which the firmware already tries, a reset line on GPIO41, and a power
rail on GPIO42 that has to be high before the reset dance. If it is genuinely
absent, everything except the service screen is unreachable.

**Battery icon full while unplugged, or the charging bolt backwards.** The
CHARGE_STATE polarity (GPIO40) is a guess; flip the comparison in
`sensors.cpp`.

**The pinned note rotates the wrong way.** The accelerometer axis mapping in
`sensors.cpp` is unverified. Turn the device slowly through all four positions,
note which way it actually goes, and tell me.

## What the serial log should say

At 115200 baud, a healthy boot is three lines:

```
touch: ok
sensors: gauge 1 rtc 1 sht 1 imu 1
font packs: 0 faces
```

`service mode` appears instead of the hub when you held a side button, or when
touch did not answer.

## What to send me

If anything is off, three things are enough for me to work from: the full
serial log from power-on, a photo of the service screen — that report block
answers most questions on its own — and which of the five corrections you ended
up saving, since that tells me what the real board does and what the built-in
defaults ought to become.
