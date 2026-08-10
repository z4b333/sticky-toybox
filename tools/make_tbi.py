#!/usr/bin/env python3
"""Turn ordinary pictures into .tbi wallpapers for the SD card.

A .tbi is the panel's own layout with an 8-byte header: 'TBI1', width, height
(little-endian u16s), then 480x800 pixels packed one bit each, MSB first,
1 = white -- exactly what the firmware's framebuffer wants, so the device
copies it instead of decoding anything.

Usage:
    python make_tbi.py photo.jpg                  # -> photo.tbi, cropped to fill
    python make_tbi.py *.png --fit                # letterboxed instead of cropped
    python make_tbi.py art.png --dither none      # hard threshold, for line art
    python make_tbi.py cover.jpg --out /sdcard/wallpapers/

Put the results in the card's root or in /wallpapers, and choose them on the
device under settings > wallpaper. What you see in the preview window of the
phone uploader and what this writes are the same idea: the finished picture,
dithered here where there is a real CPU to do it, not on the device.
"""
import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow:  pip install Pillow")

W, H = 480, 800


def convert(path: str, fit: bool, dither: str, outdir: str | None) -> str:
    im = Image.open(path)
    im = im.convert("L")

    # Fill (crop) by default: a wallpaper wants the whole panel, and a
    # letterboxed photo on 1-bit e-paper reads as a mistake. --fit keeps the
    # whole picture for things like posters where the edges matter.
    sw, sh = im.size
    scale = (min if fit else max)(W / sw, H / sh)
    nw, nh = round(sw * scale), round(sh * scale)
    im = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("L", (W, H), 255)
    canvas.paste(im, ((W - nw) // 2, (H - nh) // 2))

    if dither == "none":
        out = canvas.point(lambda v: 255 if v >= 128 else 0, mode="1")
    else:
        out = canvas.convert("1")  # Floyd-Steinberg

    data = bytearray()
    data += b"TBI1"
    data += W.to_bytes(2, "little") + H.to_bytes(2, "little")
    px = out.load()
    for y in range(H):
        for xb in range(W // 8):
            b = 0
            for k in range(8):
                if px[xb * 8 + k, y]:
                    b |= 0x80 >> k
            data.append(b)

    base = os.path.splitext(os.path.basename(path))[0] + ".tbi"
    dest = os.path.join(outdir, base) if outdir else os.path.join(os.path.dirname(path) or ".", base)
    with open(dest, "wb") as f:
        f.write(data)
    return dest


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="+", help="pictures to convert")
    ap.add_argument("--fit", action="store_true", help="letterbox instead of cropping to fill")
    ap.add_argument("--dither", choices=["fs", "none"], default="fs",
                    help="fs = photo dither (default), none = hard threshold for line art")
    ap.add_argument("--out", help="directory to write .tbi files into")
    args = ap.parse_args()
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    for p in args.images:
        dest = convert(p, args.fit, args.dither, args.out)
        print(f"{p} -> {dest}  ({os.path.getsize(dest)} bytes)")


if __name__ == "__main__":
    main()
