# Simulator placeholder assets

Drop image files here and the simulator picks them up at startup for
album art, notification icons, etc. The watch's GC9A01 panel speaks raw
RGB565, and the firmware stores album-art / notification-icon images as
loose RGB565 buffers in PSRAM — so the format the simulator expects is
the same: a tiny binary file containing the pixel grid.

## File format

`.rgb565` files: 4-byte little-endian header followed by pixels.

```
uint16  width
uint16  height
uint16  pixel[w * h]   // RGB565, little-endian, row-major
```

## Filenames the loader looks for

| File              | Used for                  | Recommended size |
| ----------------- | ------------------------- | ---------------- |
| `album_art.rgb565`| music screen background   | 240 × 240        |
| `notif_icon.rgb565`| pre-loaded notification's icon | 48 × 48     |

If a file is missing, the loader falls back to a procedurally-generated
placeholder (gradient for album art, coloured roundel for notif icon).

## Converting from PNG / JPG

`png_to_rgb565.py` in this folder converts any image Pillow can read:

```sh
pip install Pillow                                  # one-time
python simulator/placeholders/png_to_rgb565.py \
    my_album.png album_art.rgb565 240
```

The third arg is the target square size (resize+crop to fit). Omit it
to keep the source dimensions.

## How edits land

- **Native sim:** placeholders are read at startup from this directory
  by absolute path. Save, relaunch (Cmd-Q the sim and click Simulate
  again).
- **Web sim:** files are bundled into the WASM at build time via emcc's
  `--preload-file`. Save, rebuild (`simulator/web/build.sh`), refresh.
