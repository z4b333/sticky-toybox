# How Toybox handles wallpapers and the lock screen

For anyone producing pictures for this device — the PC converter, the
wallpapers site, or another firmware reading the same card. Current as of
v1.0.0-beta.41.

## Two source formats

**`.bmp` — the shared format.** Plain Windows BMP is what the
CrossPoint / CrossInk / Xteink family trades sleep art in, so a card set up
for any of those firmwares now just works here. The device parses
1/2/4/8/24/32 bpp uncompressed BMPs (BI_RGB, plus BI_BITFIELDS for 32-bit),
bottom-up or top-down rows, up to 4096×4096. The palette is read, never
assumed — an inverted-palette file shows correctly. Any size is accepted:
the picture is box-average scaled, aspect-fit on white, into 480×800
(enlarged at most 4×), then dithered **on the device** (Atkinson) to
whichever depth its destination needs.

**`.tbi` — the prepared format.** The finished framebuffer, made on a PC
(`tools/make_tbi.py` or the wallpaper page on the flasher site) where the
dither is previewable:

| field  | bytes | value                                  |
|--------|-------|----------------------------------------|
| magic  | 4     | `TBI1` (little-endian u32 0x31494254)  |
| width  | 2     | u16 LE, must be 480                    |
| height | 2     | u16 LE, must be 800                    |
| bits   | 48000 | 1 bpp, row-major, 60 bytes per row     |

Total: exactly **48,008 bytes**. Bit order **MSB first**; **1 = white**.
Any other size, magic or geometry is rejected — wrong-sized files are not
even listed. Portrait only; the device rotates through its canvas.

A prepared `.tbi` still gives the best 1-bit result (you saw the dither
before saving). A `.bmp` gives the best *grey* result — see below.

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

For a 1-bit `.tbi`: emit exactly the table above, dither on the PC, and
show the user the dithered result before saving. Name it ≤ 39 bytes and
put it in `/wallpapers`, or offer it to the phone uploader unchanged.

For grey art: save a plain uncompressed BMP (any common depth; 480×800 is
ideal, anything else is scaled) as `/sleep.bmp` or into `/.sleep/`. The
device does the grey dither itself, identically every time.

(CrossPoint's `.pxc` sleep images are a different format with **inverted**
grey levels — do not reuse one for the other. The `.bmp` route replaces
any need for it.)
