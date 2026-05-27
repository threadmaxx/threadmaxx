#!/usr/bin/env python3
"""TOU .SHP body-format reverse-engineering spike.

Companion tool to the C++ importer (`../tou2d_import_shp.cpp`). The
importer's `ShpHeader.hpp` decodes everything up to and including the
W/H/rotation anchor; the body that follows is dominated by `XX XX YY`
triplets interspersed with long zero stretches and remains opaque.

This script is intentionally Python (stdlib only — no Pillow needed)
because reverse-engineering is faster when you can iterate hypotheses
in seconds. It loads a single SHP, parses the same header bytes the
C++ parser does, then tries multiple decode hypotheses against the
body and dumps the first few frames as PPMs (magenta = transparent).

Run:
    python3 decode_spike.py [path/to/foo.SHP]

Default input is TOU/ships/PERH.SHP relative to the repo root.
Output goes to /tmp/tou2d_decode/ as `variant_<X>_<stem>_f<NN>.ppm`.

# Hypotheses tested so far (all REJECTED for PERH)

The first 269 bytes of PERH.body are all zero. Any decoder that
interprets `0x00` as a row/frame terminator will eat 24 fake-empty
frames in the leading zeros before ever reaching real pixel data;
this is why all three variants below decode "24 frames" but produce
zero opaque pixels in frame 0.

| Variant | Rule                                                | Result   |
| ------- | --------------------------------------------------- | -------- |
| A       | row = `[skip][run][color]...` until 0x00 terminator | 0 opaque |
| B       | row = `[N_runs][triplet * N_runs]`                  | 0 opaque |
| C       | classic RLE: `[count][color]`, row wraps at width   | 0 opaque |

# What the data tells us anyway

Even with the decoder broken, the byte-region analysis is meaningful:

* 2119 non-zero "regions" (maximal runs of non-zero bytes) in PERH's
  body. That's far more than 24 rotations × ~M anim frames worth of
  rows — so regions are not 1:1 with rows, and the zero-gaps between
  them are not necessarily row separators.
* Region sizes cluster: 1-byte and 3-byte regions intermixed with
  longer (7, 12, 18, 27-byte) regions. The longer regions visibly
  contain `XX XX YY` triplets in clean groups; the 1- and 3-byte
  regions don't — those might be inter-row or inter-frame metadata.
* No body size in the 9-ship corpus divides cleanly by any plausible
  `W * H * N` (uncompressed) — confirming compression.

# Next things to try (when this gets picked up again)

1. **Decode body[269:] standalone.** Maybe body[0:269] is a per-frame
   offset table (24 rotations × ~11 bytes/entry?) or padding. Treat
   269 as the real start-of-data and re-run the variants.
2. **Look at SHIPAL.COL** vs `Pal.col` — the ship-rendering palette
   may differ from the general game palette, and that would shift
   which palette indices are visually "background".
3. **Cross-reference with TOU's `data/all3.gfx` / `data/explode.gfx`.**
   Those are also opaque containers but may share the same encoding;
   matching patterns across files could expose the format.
4. **Look for embedded length-prefix fields.** Region 1 starts with
   `11 01 ...` — maybe `11` (= 17) is a length-of-frame-block byte.
5. **Interactive visualizer.** If the structure resists static
   analysis, an interactive (matplotlib?) browser that lets you
   step through decoder offsets and see partial frames in real time
   is probably the fastest path to insight.

The honest assessment after this spike: the format isn't going to
yield to pattern-matching alone. Future RE either needs an oracle
(another TOU-format implementation, the game's source) or a serious
empirical session with the visualizer above.
"""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SHP = REPO_ROOT / "TOU" / "ships" / "PERH.SHP"
PAL_PATH    = REPO_ROOT / "TOU" / "data" / "Pal.col"
OUT_DIR     = Path("/tmp/tou2d_decode")


def load_palette(path):
    raw = path.read_bytes()
    assert len(raw) == 768, f"palette must be 768 bytes, got {len(raw)}"
    pal = []
    for i in range(256):
        r6 = raw[i * 3 + 0] & 0x3F
        g6 = raw[i * 3 + 1] & 0x3F
        b6 = raw[i * 3 + 2] & 0x3F
        # 6-to-8 bit replication (mirrors ShpHeader's expand6to8)
        pal.append((
            (r6 << 2) | (r6 >> 4),
            (g6 << 2) | (g6 >> 4),
            (b6 << 2) | (b6 >> 4),
        ))
    return pal


def find_anchor(data, start=8):
    """Find 'WW 00 HH 00 18 20' anchor (mirrors ShpHeader.hpp)."""
    for i in range(start, min(start + 96, len(data) - 6)):
        if (data[i + 1] == 0 and data[i + 3] == 0 and
                data[i + 4] == 0x18 and data[i + 5] == 0x20 and
                data[i] != 0 and data[i + 2] != 0):
            return i
    raise RuntimeError("anchor not found")


def parse_header(data):
    assert data[0] == 0
    name_end = 1
    while name_end < len(data) and data[name_end] != 0:
        name_end += 1
    name = data[1:name_end].decode("ascii")
    stat = data[name_end + 1:name_end + 4]
    extra = data[name_end + 4:name_end + 8]
    anchor = find_anchor(data, name_end + 8)
    return {
        "name": name,
        "stat": stat,
        "extra": extra,
        "max_hp": extra[3],
        "frame_w": data[anchor] | (data[anchor + 1] << 8),
        "frame_h": data[anchor + 2] | (data[anchor + 3] << 8),
        "rotations": data[anchor + 4],
        "anchor": anchor,
        "body_start": anchor + 6,
    }


def write_ppm(path, w, h, pixels, pal):
    """pixels = flat list of palette indices; -1 = transparent (magenta)."""
    with open(path, "wb") as f:
        f.write(f"P6\n{w} {h}\n255\n".encode())
        for idx in pixels:
            if idx < 0:
                f.write(b"\xff\x00\xff")
            else:
                r, g, b = pal[idx]
                f.write(bytes([r, g, b]))


# ----- Variant A: per-row 0x00 terminator -----
def decode_variant_a(body, w, h, num_frames):
    frames, pos = [], 0
    for fi in range(num_frames):
        pixels = [-1] * (w * h)
        for row in range(h):
            col = 0
            while pos < len(body):
                b = body[pos]; pos += 1
                if b == 0:
                    break  # row terminator
                if pos + 1 >= len(body):
                    return frames, pos, f"truncated f{fi} r{row}"
                run = body[pos]; pos += 1
                color = body[pos]; pos += 1
                col += b
                for _ in range(run):
                    if col >= w:
                        break
                    pixels[row * w + col] = color
                    col += 1
        frames.append(pixels)
    return frames, pos, "ok"


# ----- Variant B: first-byte-of-row = num_runs -----
def decode_variant_b(body, w, h, num_frames):
    frames, pos = [], 0
    for fi in range(num_frames):
        pixels = [-1] * (w * h)
        for row in range(h):
            if pos >= len(body):
                return frames, pos, f"out f{fi} r{row}"
            n_runs = body[pos]; pos += 1
            col = 0
            for _ in range(n_runs):
                if pos + 2 >= len(body):
                    return frames, pos, "truncated"
                skip = body[pos]; pos += 1
                run = body[pos]; pos += 1
                color = body[pos]; pos += 1
                col += skip
                for _ in range(run):
                    if col >= w:
                        break
                    pixels[row * w + col] = color
                    col += 1
        frames.append(pixels)
    return frames, pos, "ok"


# ----- Variant C: classic [run][color] until row width filled -----
def decode_variant_c(body, w, h, num_frames):
    frames, pos = [], 0
    for fi in range(num_frames):
        pixels = [-1] * (w * h)
        for row in range(h):
            col = 0
            while col < w:
                if pos >= len(body):
                    return frames, pos, f"out f{fi} r{row}"
                n = body[pos]; pos += 1
                if n == 0:
                    break  # row terminator
                if pos >= len(body):
                    return frames, pos, "truncated"
                c = body[pos]; pos += 1
                for _ in range(n):
                    if col >= w:
                        break
                    pixels[row * w + col] = c
                    col += 1
        frames.append(pixels)
    return frames, pos, "ok"


# ----- Variant D: pure [skip][run][color] triplet stream, no delimiters -----
# Pixels are a linear stream of len `w*h*num_frames`. `skip` advances
# the linear cursor past transparent pixels; `run` writes `run` pixels
# of `color`. `[0][0][0]` triplets are no-ops (consume 3 bytes, write
# nothing) — which would account for the long zero stretches.
def decode_variant_d(body, w, h, num_frames, start_offset=0):
    total = w * h * num_frames
    pixels = [-1] * total
    cursor = 0
    pos = start_offset
    while pos + 2 < len(body) and cursor < total:
        skip = body[pos]
        run = body[pos + 1]
        color = body[pos + 2]
        pos += 3
        cursor += skip
        for _ in range(run):
            if cursor >= total:
                break
            pixels[cursor] = color
            cursor += 1
    # Re-slice into per-frame chunks
    frames = []
    for fi in range(num_frames):
        s = fi * w * h
        frames.append(pixels[s:s + w * h])
    return frames, pos, ("ok" if cursor >= total else f"underrun cursor={cursor}")


def region_summary(body, limit=20):
    """List the first `limit` non-zero byte regions in body."""
    regions, i = [], 0
    while i < len(body):
        while i < len(body) and body[i] == 0:
            i += 1
        start = i
        while i < len(body) and body[i] != 0:
            i += 1
        if start < i:
            regions.append((start, i))
    print(f"non-zero regions: {len(regions)}")
    for s, e in regions[:limit]:
        hex_bytes = " ".join(f"{b:02x}" for b in body[s:min(s + 20, e)])
        print(f"  [{s:5d}..{e:5d}] len={e - s:3d}  {hex_bytes}"
              f"{'...' if e - s > 20 else ''}")


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    shp_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SHP
    pal = load_palette(PAL_PATH)
    data = shp_path.read_bytes()
    hdr = parse_header(data)
    print(f"=== {shp_path.name} ===")
    print(f"header: {hdr}")
    body = data[hdr['body_start']:]
    print(f"body size: {len(body)}")
    first_nz = next((i for i, b in enumerate(body) if b != 0), None)
    print(f"first non-zero byte in body at offset: {first_nz}")
    region_summary(body, limit=12)

    w, h = hdr["frame_w"], hdr["frame_h"]
    stem = shp_path.stem.lower()
    for name, decoder in [
        ("A", decode_variant_a),
        ("B", decode_variant_b),
        ("C", decode_variant_c),
    ]:
        print(f"\n--- Variant {name} ---")
        frames, consumed, status = decoder(body, w, h, 24)
        print(f"frames: {len(frames)} consumed: {consumed}/{len(body)} "
              f"status: {status}")
        if frames:
            opaque = sum(1 for p in frames[0] if p >= 0)
            print(f"frame 0 opaque pixels: {opaque}/{w * h}")
            for i in (0, 6, 12, 18):
                if i < len(frames):
                    out = OUT_DIR / f"variant_{name}_{stem}_f{i:02d}.ppm"
                    write_ppm(out, w, h, frames[i], pal)

    # Variant D — sweep across reasonable num_frames hypotheses to see
    # if pixel coverage gets close to "filled" (= the triplet stream
    # actually fits exactly num_frames * w * h pixels).
    print(f"\n--- Variant D (pure [skip][run][color] triplet stream) ---")
    for num_frames in (24, 48, 72, 96, 24 * 8):
        frames, consumed, status = decode_variant_d(body, w, h, num_frames)
        per_frame_opaque = [sum(1 for p in f if p >= 0) for f in frames]
        total_opaque = sum(per_frame_opaque)
        print(f"  num_frames={num_frames:4d}  consumed={consumed:6d}/{len(body)}  "
              f"opaque={total_opaque:6d}/{w * h * num_frames}  "
              f"f0={per_frame_opaque[0]} f{num_frames - 1}={per_frame_opaque[-1]}  "
              f"status={status}")
        if num_frames == 96:
            for i in (0, 6, 24, 48, 95):
                if i < len(frames):
                    out = OUT_DIR / f"variant_D_{stem}_n{num_frames}_f{i:03d}.ppm"
                    write_ppm(out, w, h, frames[i], pal)


if __name__ == "__main__":
    main()
