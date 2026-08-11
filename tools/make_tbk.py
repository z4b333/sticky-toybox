#!/usr/bin/env python3
"""Turn a CBZ (or a folder of images) into a .tbk book for the SD card.

A .tbk is a 64-byte header and then fixed-size pages, each 480x800 packed one
bit per pixel in the device framebuffer's own convention (MSB first,
1 = white). Page N starts at 64 + N*48000: the reader seeks, reads, blits.
No decoder, no index, nothing to go wrong at 2 a.m. on a fridge magnet.

Header:
    0   'TBK1'
    4   u16 width  = 480        (little-endian)
    6   u16 height = 800
    8   u8  bpp    = 1
    9   u8  flags  bit0 = right-to-left (manga)
    10  u16 reserved
    12  u32 pageCount
    16  u32 pageBytes = 48000
    20  u32 dataOffset = 64
    24  char[40] title, UTF-8, NUL-padded

Usage:
    python make_tbk.py volume1.cbz --rtl --title "One Piece 1"
    python make_tbk.py scans/ --out E:/books/
    python make_tbk.py pages.cbz --trim --dither none

--trim crops white margins per page before scaling: scans carry generous
borders, and removing them puts 10-15% more resolution where it matters,
which is small dialogue. --rtl marks manga; the reader swaps its tap zones.
"""
import argparse
import io
import os
import re
import sys
import zipfile

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow:  pip install Pillow")

W, H = 480, 800
PAGE_BYTES = W * H // 8


def natural_key(s: str):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]


def load_pages(src: str):
    """Yield PIL images in reading order from a CBZ or a folder."""
    exts = (".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp")
    if os.path.isdir(src):
        names = sorted((n for n in os.listdir(src) if n.lower().endswith(exts)), key=natural_key)
        for n in names:
            yield Image.open(os.path.join(src, n))
    else:
        with zipfile.ZipFile(src) as z:
            names = sorted((n for n in z.namelist() if n.lower().endswith(exts)), key=natural_key)
            for n in names:
                yield Image.open(io.BytesIO(z.read(n)))


def trim_margins(im: Image.Image) -> Image.Image:
    """Crop the white border, keeping a small breath of margin."""
    gray = im.convert("L")
    mask = gray.point(lambda v: 0 if v > 235 else 255)
    box = mask.getbbox()
    if not box:
        return im
    pad = 8
    l, t, r, b = box
    return im.crop((max(0, l - pad), max(0, t - pad),
                    min(im.width, r + pad), min(im.height, b + pad)))


def to_page(im: Image.Image, trim: bool, dither: str) -> bytes:
    im = im.convert("L")
    if trim:
        im = trim_margins(im)
    scale = min(W / im.width, H / im.height)
    nw, nh = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    im = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("L", (W, H), 255)
    canvas.paste(im, ((W - nw) // 2, (H - nh) // 2))
    if dither == "none":
        out = canvas.point(lambda v: 255 if v >= 128 else 0, mode="1")
    else:
        out = canvas.convert("1")  # Floyd-Steinberg
    data = bytearray(PAGE_BYTES)
    px = out.load()
    for y in range(H):
        base = y * (W // 8)
        for xb in range(W // 8):
            b = 0
            for k in range(8):
                if px[xb * 8 + k, y]:
                    b |= 0x80 >> k
            data[base + xb] = b
    return bytes(data)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="a .cbz file or a folder of images")
    ap.add_argument("--title", help="up to 40 bytes of UTF-8; defaults to the file name")
    ap.add_argument("--rtl", action="store_true", help="right-to-left reading order (manga)")
    ap.add_argument("--trim", action="store_true", help="crop white margins before scaling")
    ap.add_argument("--dither", choices=["fs", "none"], default="fs")
    ap.add_argument("--out", help="output directory")
    args = ap.parse_args()

    base = os.path.splitext(os.path.basename(args.source.rstrip("/\\")))[0]
    title = (args.title or base).encode("utf-8")[:40]
    dest = os.path.join(args.out or ".", base + ".tbk")
    if args.out:
        os.makedirs(args.out, exist_ok=True)

    pages = []
    for i, im in enumerate(load_pages(args.source)):
        pages.append(to_page(im, args.trim, args.dither))
        print(f"\rpage {i + 1}", end="", flush=True)
    print()
    if not pages:
        sys.exit("no images found")

    header = bytearray(64)
    header[0:4] = b"TBK1"
    header[4:6] = W.to_bytes(2, "little")
    header[6:8] = H.to_bytes(2, "little")
    header[8] = 1
    header[9] = 1 if args.rtl else 0
    header[12:16] = len(pages).to_bytes(4, "little")
    header[16:20] = PAGE_BYTES.to_bytes(4, "little")
    header[20:24] = (64).to_bytes(4, "little")
    header[24:24 + len(title)] = title

    with open(dest, "wb") as f:
        f.write(header)
        for p in pages:
            f.write(p)
    mb = os.path.getsize(dest) / 1e6
    print(f"{dest}: {len(pages)} pages, {mb:.1f} MB{'  (right-to-left)' if args.rtl else ''}")
    print("put it in the card's /books folder")


if __name__ == "__main__":
    main()
