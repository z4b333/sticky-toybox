# How Toybox handles wallpapers and the lock screen

For anyone producing pictures for this device — the PC converter, the
wallpapers site, or another firmware reading the same card. Current as of
v1.0.0-beta.41.

## One format: `.bmp`

**Everything the user puts on the card is a plain Windows BMP** — the
same file serves wallpaper, lock picture, sleep art, and (named
`<book name>.cover.bmp` beside a book) that book's cover. It is also what
the CrossPoint / CrossInk / Xteink family trades sleep art in, so a card
set up for any of those firmwares just works here. The device parses
1/2/4/8/24/32 bpp uncompressed BMPs (BI_RGB, plus BI_BITFIELDS for 32-bit),
bottom-up or top-down rows, up to 4096×4096. The palette is read, never
assumed — an inverted-palette file shows correctly. Any size is accepted:
the picture is box-average scaled, aspect-fit on white, into 480×800
(enlarged at most 4×), then dithered **on the device** (Atkinson) to
whichever depth its destination needs.

**`.tbi` — legacy, still read.** Old cards prepared for earlier releases
carry finished 1-bit framebuffers; the picker still lists and takes them,
but nothing new should produce one:

| field  | bytes | value                                  |
|--------|-------|----------------------------------------|
| magic  | 4     | `TBI1` (little-endian u32 0x31494254)  |
| width  | 2     | u16 LE, must be 480                    |
| height | 2     | u16 LE, must be 800                    |
| bits   | 48000 | 1 bpp, row-major, 60 bytes per row     |

Total: exactly **48,008 bytes**. Bit order **MSB first**; **1 = white**.
Any other size, magic or geometry is rejected — wrong-sized files are not
even listed. Portrait only; the device rotates through its canvas.

A four-level-grey `.bmp` (what the wallpaper page on the flasher site
saves) is the one file that does every job well: the greys survive where
grey is shown, and the device's own 1-bit re-dither of four flat tones at
1:1 is clean where 1-bit is needed.

## Where pictures come from

**The SD card is the front door.** The picker lists `.tbi` and `.bmp`
files from `/`, `/wallpapers`, `/sleep` and `/.sleep` (extension
case-insensitive; names shown truncated to 39 bytes). Choosing one
**copies/converts it into device flash**, so the card can come back out:

- wallpaper → 1-bit LittleFS `/wallimg.tbi` (a `.bmp` is dithered on the way)
- lock picture from a `.tbi` → LittleFS `/lockimg.tbi` (1-bit)
- lock picture from a `.bmp` → LittleFS `/lockimg.g2` (**2 bpp, four-level
  grey** — header `TBG1` + u16 480 + u16 800, then 96,000 bytes in the
  panel's 2bpp layout, high pair first, 0 = black, 3 = white; 96,008 total)

At most one lock file exists at a time: whichever way a picture last
arrived (card `.bmp`, card `.tbi`, or phone upload) removes the other, and
REMOVE clears both.

Reading the card borrows the display's SPI bus and re-initialises the panel
on release, which is why every card action is followed by a full refresh.

**The phone is the fallback** for a device with an empty card slot. The
notes tool's pairing screen serves an upload page over SoftAP; the file is
streamed straight to LittleFS (never buffered in RAM), landing at
`/lockimg.tbi` by default or `/wallimg.tbi` when the request carries
`?to=wall`. Only exactly 48,008 bytes is accepted; a half-arrived file is
deleted rather than kept.

## Sleep art, matcha's way

On power-off with the lock screen set to PICTURE and no note pinned, the
device first looks at the card:

1. `/sleep.bmp` — a fixed choice, always wins;
2. else a **random** `.bmp` from `/.sleep/`;
3. else a random `.bmp` from `/sleep/`.

A hit is rendered in **four-level grey** through the panel's grey waveform
(~3 s) with **nothing else on the panel — no time, temperature or battery
footer**. A photograph the panel can do justice to gets the whole panel.
This is the same convention matcha-reader uses, so one folder of art
serves both firmwares.

## Where pictures are drawn

**Wallpaper** (`/wallimg.tbi`): behind the hub's home screen, 1-bit,
drawn through the canvas once per visit alongside the home screen's full
refresh. Runs of white bytes are skipped, so mostly-white pictures draw
fast.

**Lock screen**: what the panel keeps after power-off. The order at
shutdown:

1. **Battery empty** overrides everything.
2. **A pinned note** — edge to edge at its pinned rotation (or following
   the accelerometer), at the owner's chosen text size.
3. Otherwise, the "with no note pinned" setting, one of:
   - **PICTURE** — card sleep art in grey (above), else `/lockimg.g2` in
     grey, else `/lockimg.tbi` in 1-bit; falls back to COVER if none
   - **COVER** — `/lockcover.tbi`, the current book's cover stashed into
     flash at book-open time (so sleep never touches the card)
   - **GOODBYE** — a text card
   - **BLANK** — nothing, deliberately
4. The time/temperature/battery footer appears only on the *canvas*
   paths (notes, 1-bit pictures, goodbye) — never over grey art.

## What a producer must do

Save a plain uncompressed BMP — a four-level-grey 8 bpp one at 480×800 is
ideal, but any common depth and size works (anything else is scaled).
Name it ≤ 39 bytes and put it in `/wallpapers` or the root for the picker,
as `/sleep.bmp` or into `/.sleep/` for sleep art, or as
`<book name>.cover.bmp` beside a book for its cover. The device does its
own dither, identically every time. Do not emit new `.tbi` files.

(CrossPoint's `.pxc` sleep images are a different format with **inverted**
grey levels — do not reuse one for the other. The `.bmp` route replaces
any need for it.)
