# Where a .tbk cover goes

Handoff spec for a PC-side tool that generates manga/book covers for Toybox
(Seeed reTerminal Sticky). Self-contained: everything needed to write a cover
the firmware will find and use, with no further questions.

Reference implementation: `tools/make_tbk.py --cover` in the `sticky-toybox`
repo. If your output ever disagrees with the device, diff against that.

---

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
