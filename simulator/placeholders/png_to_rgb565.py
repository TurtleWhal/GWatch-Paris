#!/usr/bin/env python3
"""Convert any image Pillow can read into the simulator's .rgb565 format.

Usage:
    python png_to_rgb565.py SRC [DST] [--size N]

SRC          Path to a PNG / JPG / etc.
DST          Output path. Defaults to <SRC stem>.rgb565 next to SRC.
--size N     Resize+center-crop the source to N×N before encoding. Omit
             to keep the source's native dimensions.

Examples:
    python png_to_rgb565.py cover.png                       # → cover.rgb565
    python png_to_rgb565.py cover.png album_art.rgb565
    python png_to_rgb565.py cover.png album_art.rgb565 --size 240
    python png_to_rgb565.py icon.png notif_icon.rgb565 --size 48

Format:
    u16 width        little-endian
    u16 height       little-endian
    u16 pixels[w*h]  RGB565 little-endian, row-major

Requires: pip install Pillow
"""
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow not installed. Run: pip install Pillow")


def square_crop(img: "Image.Image", size: int) -> "Image.Image":
    """Resize the shortest side to `size`, then center-crop to square."""
    w, h = img.size
    scale = size / min(w, h)
    img = img.resize((round(w * scale), round(h * scale)), Image.LANCZOS)
    w, h = img.size
    left = (w - size) // 2
    top = (h - size) // 2
    return img.crop((left, top, left + size, top + size))


def to_rgb565(img: "Image.Image") -> bytes:
    img = img.convert("RGB")
    w, h = img.size
    pixels = img.tobytes()  # raw RGB888
    out = bytearray(4 + w * h * 2)
    struct.pack_into("<HH", out, 0, w, h)
    for i in range(w * h):
        r, g, b = pixels[i * 3], pixels[i * 3 + 1], pixels[i * 3 + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[4 + i * 2] = v & 0xFF
        out[4 + i * 2 + 1] = v >> 8
    return bytes(out)


def main() -> int:
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        sys.exit(__doc__)

    # Pull --size N out of args. Anything else is positional (SRC [DST]).
    size = None
    positional = []
    i = 0
    while i < len(args):
        if args[i] == "--size" and i + 1 < len(args):
            size = int(args[i + 1])
            i += 2
        else:
            positional.append(args[i])
            i += 1

    if len(positional) < 1 or len(positional) > 2:
        sys.exit(__doc__)

    src = Path(positional[0])
    dst = Path(positional[1]) if len(positional) == 2 else src.with_suffix(".rgb565")

    img = Image.open(src)
    if size is not None:
        img = square_crop(img, size)
    dst.write_bytes(to_rgb565(img))
    w, h = img.size
    size_kb = dst.stat().st_size / 1024
    print(f"wrote {dst} ({w}x{h}, {size_kb:.1f} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
