#!/usr/bin/env python3
"""Definitive TOU .SHP sprite-body decoder (M4.7c breakthrough).

Body layout — verified empirically across all 9 stock SHP files:

    file_size = header_bytes + 32 * 3 * frame_w * frame_h
    header_bytes ≈ 592 + name_length    (varies per ship by name length)
    body_start  = file_size - 32 * 3 * frame_w * frame_h

Each frame is `3 * frame_w * frame_h` bytes, laid out as 1 pixel = 3
consecutive bytes (interleaved per-pixel triplet):

    pixel(x, y) = bytes[ (y * frame_w + x) * 3 .. (y * frame_w + x) * 3 + 3 ]
                = (b0, b1, b2)

Empirical channel meaning (visually verified on TIEF, FLYY, BATM, XWIN,
DEST):
    b0 — primary hull palette index (clean ship silhouette by itself)
    b1 — secondary highlight / wing-edge palette index
    b2 — cockpit / center detail palette index (concentrated near center)

Render order for the cleanest composite is `b2 over b0` — byte[2] takes
precedence when non-zero, else byte[0]. Palette index 0 is transparent.

The 0x18 = 24 byte in the header WH-anchor is "core rotation steps"
(unused by the decoder — body has 32, not 24); 32 rotations is the
actual sprite-sheet frame count. Each frame = 11.25° of CCW rotation
starting from "ship facing down" at frame 0.

Renders one PNG montage per ship of all 32 rotations.
"""

from pathlib import Path
from PIL import Image, ImageDraw

SHIPS_DIR = Path("/home/farkasau/git/threadmaxx/TOU/ships")
PAL_PATH  = Path("/home/farkasau/git/threadmaxx/TOU/data/Pal.col")
OUT_DIR   = Path("/tmp/tou2d_decoded")

# (filename stem, frame_w, frame_h) — frame dim from the WH anchor.
SHIPS = [
    ("FLYY", 32, 32),
    ("XWIN", 32, 32),
    ("DEST", 30, 30),
    ("BATM", 28, 28),
    ("BEE2", 28, 28),
    ("PERH", 26, 26),
    ("TIEF", 26, 26),
    ("PERU", 24, 24),
    ("SPED", 22, 22),
]

NUM_ROTATIONS = 32


def load_palette(path):
    raw = path.read_bytes()
    assert len(raw) == 768, f"palette must be 768 bytes, got {len(raw)}"
    pal = []
    for i in range(256):
        r6 = raw[i*3+0] & 0x3F
        g6 = raw[i*3+1] & 0x3F
        b6 = raw[i*3+2] & 0x3F
        pal.append(((r6<<2)|(r6>>4), (g6<<2)|(g6>>4), (b6<<2)|(b6>>4)))
    return pal


def render_frame(frame_bytes, w, h, pal):
    """Render one rotation frame as RGB. b2 over b0 priority, idx 0 = transparent."""
    img = Image.new('RGB', (w, h), (255, 0, 255))
    px = img.load()
    for y in range(h):
        for x in range(w):
            o = (y*w + x) * 3
            b0 = frame_bytes[o]
            b2 = frame_bytes[o+2]
            idx = b2 if b2 else b0
            if idx == 0:
                px[x, y] = (255, 0, 255)  # magenta = transparent
            else:
                px[x, y] = pal[idx]
    return img


def build_montage(stem, frames, w, h, upscale=4):
    cols = 8
    rows = 4
    pad = 4
    label_h = 12
    tile_w = w * upscale
    tile_h = h * upscale
    W = cols * (tile_w + pad) + pad
    H = rows * (tile_h + label_h + pad) + pad
    canvas = Image.new('RGB', (W, H), (32, 32, 32))
    draw = ImageDraw.Draw(canvas)
    for fi, img in enumerate(frames):
        img = img.resize((tile_w, tile_h), Image.NEAREST)
        r = fi // cols
        c = fi % cols
        x = pad + c * (tile_w + pad)
        y = pad + r * (tile_h + label_h + pad)
        canvas.paste(img, (x, y))
        draw.text((x + 2, y + tile_h + 1), f"{fi:02d}", fill=(255, 255, 0))
    return canvas


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    pal = load_palette(PAL_PATH)

    for stem, w, h in SHIPS:
        path = SHIPS_DIR / f"{stem}.SHP"
        data = path.read_bytes()
        frame_size = 3 * w * h
        body_size = NUM_ROTATIONS * frame_size
        body_start = len(data) - body_size

        if body_start < 0:
            print(f"!! {stem}: file too small ({len(data)} < {body_size})")
            continue

        body = data[body_start:]
        frames = []
        for fi in range(NUM_ROTATIONS):
            fb = body[fi*frame_size:(fi+1)*frame_size]
            frames.append(render_frame(fb, w, h, pal))

        montage = build_montage(stem, frames, w, h)
        out = OUT_DIR / f"{stem}_rotations.png"
        montage.save(out)
        print(f"{stem}: body@{body_start} ({len(data)} - {body_size}) -> {out}")


if __name__ == '__main__':
    main()
