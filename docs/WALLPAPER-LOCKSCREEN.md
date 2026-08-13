# How Toybox handles wallpapers and the lock screen

For anyone producing pictures for this device — the PC converter, the
wallpapers site, or another firmware reading the same card. Current as of
v1.0.0-beta.37.

## The file format: `.tbi`

One format for both pictures. The device never decodes anything — a `.tbi`
IS the framebuffer, prepared on a PC (`tools/make_tbi.py`) where scaling,
cropping, greyscale and dithering are cheap and previewable.

| field  | bytes | value                                  |
|--------|-------|----------------------------------------|
| magic  | 4     | `TBI1` (little-endian u32 0x31494254)  |
| width  | 2     | u16 LE, must be 480                    |
| height | 2     | u16 LE, must be 800                    |
| bits   | 48000 | 1 bpp, row-major, 60 bytes per row     |

Total: exactly **48,008 bytes**. Bit order is **MSB first**; **1 = white,
0 = black**. A file of any other size, magic, or geometry is rejected —
wrong-sized files are not even listed on the picker.

Portrait only (480×800). The device rotates through its canvas when needed;
the file itself is never landscape.

## Where pictures come from

**The SD card is the front door.** The picker lists `.tbi` files from `/`
and `/wallpapers` (root first, so a card without the folder still works;
extension case-insensitive; names shown truncated to 39 bytes). Choosing
one **copies it into device flash**, so the card can come back out:

- wallpaper → LittleFS `/wallimg.tbi`
- lock screen picture → LittleFS `/lockimg.tbi`

Reading the card borrows the display's SPI bus and re-initialises the panel
on release, which is why every card action is followed by a full refresh.

**The phone is the fallback** for a device with an empty card slot. The
notes tool's pairing screen serves an upload page over SoftAP; the file is
streamed straight to LittleFS (never buffered in RAM), landing at
`/lockimg.tbi` by default or `/wallimg.tbi` when the request carries
`?to=wall`. A half-arrived file is deleted rather than kept — a picture
that turns to noise partway down the panel reads as a hardware fault.
An upload is accepted only at exactly 48,008 bytes.

**Settings paths:** Settings → Wallpaper lists the card directly. Settings
→ Lock screen → THE PICTURE → `FROM CARD` opens the same list writing to
the lock file; "or send one from a phone" at the foot of that page is the
upload route. Either picture can be removed on its page; the two files are
fully independent.

## Where pictures are drawn

**Wallpaper** (`/wallimg.tbi`): behind the hub's home screen, drawn through
the canvas once per visit alongside the home screen's full refresh. The
dock is haloed against it. No wallpaper → a plain home with a one-line
hint. Runs of white bytes (0xFF) are skipped, so mostly-white pictures
draw fast.

**Lock screen**: what the panel keeps after power-off (e-paper holds its
last frame with no power). The draw order at shutdown:

1. **Battery empty** overrides everything (the panel must say why the
   device stopped answering).
2. **A pinned note**, if one exists — drawn edge to edge at its pinned
   rotation (or following the accelerometer when "turn with the device" is
   on). Notes render at the owner's chosen text size (24/32/40 px).
3. Otherwise, the "with no note pinned" setting, one of:
   - **PICTURE** — `/lockimg.tbi`; falls back to COVER if none stored
   - **COVER** — `/lockcover.tbi`, a copy of the current book's cover
     stashed into flash at book-open time (so sleep never touches the card)
   - **GOODBYE** — a text card
   - **BLANK** — nothing, deliberately
4. A footer line may carry the time, temperature and battery, per settings.

## What a producer must do

Emit exactly the table above: 480×800, 1-bit, MSB-first, 1 = white,
48,008 bytes, `TBI1` header. Dither on the PC and show the user the
dithered result before saving — one-bit e-paper turns a badly chosen
photograph into mud, and the only way to know is to look. Name the file
something short (≤ 39 bytes survives the picker) and put it in
`/wallpapers` on the card, or offer it to the phone uploader unchanged.

There is no 2-bit (grey) wallpaper or lock format. Grey exists on this
panel only as a full 3-second waveform used by the book reader; the lock
and wallpaper paths are 1-bit by design. (CrossPoint's `.pxc` sleep images
are a different format with **inverted** grey levels — do not reuse one
for the other.)
