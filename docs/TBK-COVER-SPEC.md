# Where a book cover goes

Handoff spec for a PC-side tool that generates manga/book covers for Toybox
(Seeed reTerminal Sticky). Self-contained: everything needed to write a cover
the firmware will find and use, with no further questions.

Reference implementation: `tools/make_tbk.py --cover` in the `sticky-toybox`
repo. If your output ever disagrees with the device, diff against that.

---

## Two ways, and the sidecar is the better one

**A `.cover.tbi` beside the book** works for **both** `.epub` and `.tbk`, wins
over anything inside the file, and is picked up whenever it changes. This is
the recommended route: a desktop has the whole image, a real dithering
library and no memory ceiling, where the device has a streaming decoder and a
band of RAM. Line art especially comes out badly on-device — Floyd–Steinberg
is a photographic algorithm and turns a flat grey background into a field of
worms. It also makes the book open faster, because there is nothing to decode.

    /books/Uketsu/strange-houses.epub
    /books/Uketsu/strange-houses.cover.tbi      <- same stem, ".cover.tbi"

The file is the same format as a wallpaper or lock screen picture:

```
offset  0   char[4]  "TBI1"
        4   u16      width  = 480     (little-endian)
        6   u16      height = 800
        8   ...      48,000 bytes of 1-bit pixels
```

Pixels are exactly as described below for the embedded cover: row-major,
60 bytes a row, MSB first, **1 = white, 0 = black**. Total file size is
**48,008 bytes**.

The device compares 64 bytes of the sidecar against the cover it has stored,
so replacing the `.tbi` on the card is picked up the next time the book is
opened. No need to delete anything.

The rest of this document describes the **embedded** cover — carried inside a
`.tbk`. It is still supported and still useful (it survives the book being
moved, and needs no second file), but for anything where the artwork matters,
the sidecar is easier to iterate on.

## The short answer

**Inside the `.tbk` file, at byte offset 64.** Not a separate file, not a
folder on the card.

```
byte 0      64                 48064
|  header  |     cover        |  page 0  |  page 1  | ...
   64 B         48,000 B         48,000 B each (1-bit)
```

## What changes in the header

Two fields. Everything else stays as it already is.

| offset | field | value |
|---|---|---|
| 9 | `flags` | bit 0 = right-to-left (as before), **bit 1 = a cover follows the header** |
| 20 | `dataOffset` (u32 LE) | `64` with no cover, **`48064`** with one |

`dataOffset` was always in the format and always written as `64`; the firmware
used to assume it rather than read it. It is read now, which is what lets the
cover sit between the header and the pages.

Full header for reference:

```
0   char[4]  "TBK1"
4   u16      width  = 480          (little-endian throughout)
6   u16      height = 800
8   u8       bpp    = 1 or 2       (pages only; the cover is always 1)
9   u8       flags  bit0 = rtl, bit1 = cover present
10  u16      reserved = 0
12  u32      pageCount
16  u32      pageBytes = 48000 * bpp
20  u32      dataOffset = 64, or 48064 with a cover
24  char[40] title, UTF-8, NUL-padded
```

## The cover block

Exactly **48,000 bytes**, no header of its own:

- 480 wide × 800 tall
- **1 bit per pixel**, even when the book's pages are 2-bpp grey — the loading
  screen and the hub's strip both draw through the ordinary canvas, so a grey
  cover would only be dithered on the device anyway
- Row-major, top to bottom; **60 bytes per row**
- Within a byte, **most significant bit is the leftmost pixel**
- **1 = white, 0 = black**

This is the same layout as a page, so a tool that already emits pages already
emits covers.

## How to prepare the image

Match what the device would do, so a PC-made cover and a device-made one look
like siblings:

1. Convert to greyscale (`L`).
2. **Aspect-fit** into 480×800 — scale by `min(480/w, 800/h)`, Lanczos.
3. **Centre on a white canvas.** Do not crop to fill; a cover with its title
   sliced off is worse than one with margins.
4. **Floyd–Steinberg dither** to 1-bit.
5. **No contrast stretch.** Tested on real covers: stretching helps at
   thumbnail size and hurts at full size, and the device applies it only when
   it shrinks the cover for the 96×160 hub strip.

```python
from PIL import Image

W, H = 480, 800

def make_cover(path: str) -> bytes:
    im = Image.open(path).convert("L")
    s = min(W / im.width, H / im.height)
    nw, nh = max(1, round(im.width * s)), max(1, round(im.height * s))
    im = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("L", (W, H), 255)
    canvas.paste(im, ((W - nw) // 2, (H - nh) // 2))
    bw = canvas.convert("1")              # Floyd-Steinberg by default
    px = bw.load()
    out = bytearray(W * H // 8)
    for y in range(H):
        base = y * (W // 8)
        for xb in range(W // 8):
            b = 0
            for k in range(8):
                if px[xb * 8 + k, y]:     # non-zero = white = bit set
                    b |= 0x80 >> k
            out[base + xb] = b
    return bytes(out)                     # exactly 48,000 bytes
```

## Two rules the firmware enforces

Get these wrong and the cover is silently ignored:

- **`dataOffset` below 64 is read as 64, not refused.** Files exist whose
  converter never filled the field in, and refusing them would break books
  that work today.
- **If bit 1 is set but `dataOffset < 48064`, the cover flag is dropped.**
  Believing an impossible claim would read page 0 out of the middle of the
  cover, and a book that opens on garbage is worse than one that opens
  plainly. **Set both fields or neither.**

Still refused outright, as before: wrong magic, dimensions other than
480×800, `bpp` outside {1, 2}, a `pageBytes` that contradicts `bpp`, or
`pageCount` of zero.

## What the device does with it

- **Loading screen** — painted full-size while the book opens, before the card
  is even touched.
- **Hub "recently read" strip** — shrunk to 96×160 (averaged, then stretched
  2–98 percentile and thresholded) and kept in the device's flash.
- **Lock screen** — if the owner sets the sleeping panel to COVER, the cover of
  the book they are reading is what the panel wears.

All three are built once, on the book's first open, and cached afterwards.

## What NOT to do

**Do not write to `/.toybox/covers/<hash>.tbc` on the card.** That folder is
the device's own cache. Two reasons it is the wrong target:

1. The firmware decides whether a book has a cover by looking at a thumbnail
   in its *internal flash*, which a PC cannot write. It would see none,
   rebuild from the book, and overwrite your file.
2. The name is a hash of the book's absolute card path, so the cover is
   orphaned the moment the book is renamed or moved into a series folder —
   the same way its reading position is.

A cover carried inside the `.tbk` has neither problem: it travels with the
book, survives renames, and needs no hash.

## Where the finished book goes

The card's `/books` folder, or a series folder under it
(`/books/One Piece/vol01.tbk`) — the readers show those folders as series.

---

# Artwork inside an EPUB

Light novels carry character art and story illustrations, and the reader can
show them. Same principle as the cover sidecar: **the desktop makes the
picture, the device blits it.**

## What the app adds to the book

For every image the device should display, add ONE extra entry to the EPUB's
zip, at the original entry's path with a `toybox/` prefix and a `.tbi`
extension:

```
OEBPS/images/insert-01.jpg          <- the original, left exactly as it is
toybox/OEBPS/images/insert-01.tbi   <- what the device draws
```

Nothing else in the book changes. The XHTML keeps its ordinary
`<img src="images/insert-01.jpg">`, the original image stays where it is, and
every other reader ignores the extra entry — a zip may carry files the OPF
manifest never mentions. **The book stays a valid EPUB.**

Store the `.tbi` entries **uncompressed** (zip method 0) if your library lets
you choose. They are already 1-bit and compress by only a few percent, and a
stored entry means the device seeks straight to the pixels instead of
inflating 48 KB to find them.

## The .tbi itself

Identical to a cover sidecar — the same format as a wallpaper:

```
offset  0   char[4]  "TBI1"
        4   u16      width  = 480     (little-endian)
        6   u16      height = 800
        8   ...      48,000 bytes of 1-bit pixels
```

Row-major, 60 bytes a row, MSB first, **1 = white, 0 = black**. Exactly
**48,008 bytes**.

## Preparing the picture

An illustration gets **its own page** on the device — 480×800, the whole
glass, with the text resuming after it. So fit the artwork into 480×800 and
centre it on white, exactly as for a cover.

The one thing worth doing differently from the cover recipe: **choose the
treatment to match the art.** Photographic art wants Floyd–Steinberg with no
contrast stretch. Line art, screentoned manga panels and flat-coloured
character art usually want a contrast stretch first, and sometimes a plain
threshold instead of a dither — a dither turns large flat areas into visible
texture. Your app can show the result before it writes it; the device cannot.

## What the device does without one

Today: the page says so. The reader shows a plate naming the image it cannot
draw — "this book has no picture prepared for it" — rather than a blank page
or a silently skipped illustration, because a reader who can see the name can
go and prepare it.

Decoding the original on the device (baseline JPEG, progressive JPEG and PNG
are all already decodable here — that is how EPUB covers are built) is the
next step, and the format above does not change when it arrives: a `toybox/`
entry will still win, because a picture dithered on a desktop beats one
dithered in a 2 KB band. Anything you care about the look of is worth
pre-rendering either way.

## What the device ignores

- Images inside `<head>`, or in any non-visible element.
- SVG. There is no vector renderer, and there will not be one.
- An entry that is not exactly 48,008 bytes, or whose header is not `TBI1`
  480×800 — it falls back to decoding the original rather than drawing
  something wrong.
