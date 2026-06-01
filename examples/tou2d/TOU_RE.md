# TOU_RE — reverse-engineering notes

Findings from inspecting the original *TOU* (v1.0, GigaMess / Hannu
Kankaanpää, 2002) install for the purposes of asset compatibility in
`examples/tou2d/`. Distilled out of the original `TOU_PLAN.md` on
2026-05-31 so the live plan stays small while the archaeology stays
preserved.

**Hard line**: nothing from `TOU.exe` is disassembled or copied as code.
All compatibility flows through on-disk asset formats, which are public
via `toudoc_makelev.htm` shipped with the original game. The `TOU/`
install directory itself is non-redistributable — treat as an
inspection-only artifact on the local box.

The four shipped `.lev`s + nine shipped `.SHP`s + GG theme directories
are the corpus this document was derived from. Every fact below was
either confirmed by hex inspection / round-trip importer test, or
distilled from the bundled HTML documentation. Where a section is
**still TBD** that's called out inline.

---

## 1. Install-directory inventory

What `../../TOU/` (relative to repo root) actually contains. Pinning
here so future RE batches don't re-derive.

### Top-level files
- `TOU.exe` (512 KB) — original Windows binary. Opaque; runtime oracle
  only, never disassembled.
- `options.cfg` (≈ 6 KB) — opaque binary blob (mostly zero-padded with
  embedded XOR-or-compressed payload). Not worth supporting; tou2d uses
  its own `settings.dat` format.
- `readme.txt` / `file_id.diz` / `toudoc*.htm` — bundled documentation.
  The HTML pages are the authoritative source for weapon behaviors,
  ship stats, and control bindings. See § 4 below for the distilled
  gameplay reference.
- `fmod.dll`, `ijl10.dll` — third-party audio (FMOD) and JPEG (Intel
  JPEG Library) DLLs. Not redistributable; replaced with `miniaudio` +
  `stb_image`.

### `levels/` — 4 binary `.lev` containers

| File | Size | Visible header |
|---|---|---|
| `desert.lev` | 161 KB | `TOU level file v1.4\r\n\x1a` |
| `jungle.lev` | 157 KB | same magic, author "Hannu Kankaanpää" |
| `minibase.lev` | 83 KB | same magic |
| `woods.lev` | 200 KB | same magic |

See § 2 below for the format breakdown.

### `ships/` — 9 ship designs

| File | Size | Display name (from leading string) |
|---|---|---|
| `BATM.SHP` | 75 KB | "Batman ship" |
| `BEE2.SHP` | 75 KB | "B2 Stealth fighter" |
| `DEST.SHP` | 87 KB | "Destroyer" |
| `FLYY.SHP` | 98 KB | "Fly" |
| `PERH.SHP` | 65 KB | "Butterfly" |
| `PERU.SHP` | 55 KB | "Basic TOU ship" |
| `SPED.SHP` | 47 KB | "Speedie" |
| `TIEF.SHP` | 65 KB | "Imperium Tie Fighter" |
| `XWIN.SHP` | 98 KB | "X-Wing fighter" |

> Note: file display names DON'T match the manual's stock-ship table
> (manual says "Basic ship", "Bee", "Tie Fighter", etc.; files say
> "Butterfly", "B2 Stealth fighter", "Imperium Tie Fighter"). The
> `ManualEntry` table in the importer CLI was therefore unreliable for
> stem → display-name mapping; the parser's `displayName` field is
> authoritative.

See § 3 below for the format breakdown.

### `data/` — palettes, fonts, menu graphics

| File | Purpose | Status |
|---|---|---|
| `Pal.col`, `SHIPAL.COL` | 8-bit palettes (256×3 bytes) | Decoded (`PalCol.hpp`) |
| `f_large.tga`, `f_med.tga`, `f_mini.tga`, `f_tiny5d.tga` | Bitmap fonts | Not decoded — tou2d redraws fonts from scratch |
| `menu3d.jpg`, `splay.jpg`, `sanim2.jpg` | Menu background art | Not used — tou2d ships its own menus |
| `all3.gfx`, `explode.gfx` | Sprite atlases (proprietary `.gfx`) | Opaque — not decoded |
| `NAMES.DAT` | Probably bot / ship random-name pool | Opaque |
| `loadtime.dat`, `taulu2.tau` | Unknown | Opaque |

Palettes are interesting only if pixel-perfect re-rendering of the
original ship sprites is wanted — the M4.7d color model (see § 3.4)
showed that ship sprites don't actually need the VGA palette anyway.

### `ggstuff/` — 7 procedural-level (GG) themes

Six theme directories: `happyland`, `rocky mountains`, `the beach`,
`the diablo`, `the earth`, `winter`. Each has the standard tagged-naming
asset set documented in `toudoc_makelev.htm`:

| Tag | Purpose | Format |
|---|---|---|
| `s1.tga`, `s2.tga`, … | Filler shape (black = empty, non-black = solid) | 24-bit uncompressed TGA |
| `t1.jpg` | Texture | JPEG |
| `l1.tga`, `l2.tga`, … | Lawn / grass (optional) | TGA |
| `d1.tga`, … | Repair-pad sprite (optional) | TGA |
| `x1.tga`, … | Sign / billboard (optional) | TGA |
| `p1.tga`, `p2.tga`, … | Decoration sprites | TGA |
| `sd1.tga`, … | Extra ground-bottom shapes | TGA |
| `px1.jpg` | Parallax background (optional) | JPEG |
| `ex1.jpg` | Explosive-land texture (optional) | JPEG |
| `info.txt` | Theme config (`/MAKER`, `/TEXDARK`, `/PARDARK`, `/TEXATTR`, `/GRASATTR`, …) | line-based ASCII |

**This is the easiest compatibility win.** The directory layout is
filesystem-native, the formats are open (TGA + JPEG), and `info.txt` is
human-readable. Drop a TOU theme directory into
`examples/tou2d/assets/themes/` and tou2d picks it up with zero
conversion.

### `sfx/` and `music/`

| Dir | Contents |
|---|---|
| `sfx/` | ≈ 100 `.wav` files, 8-character DOS-style names, naming convention `<cat>_<intensity>.wav` — `r` (random?), `m` (medium), `h` (heavy), `l` (low). |
| `music/` | 6 `.ogg` files: `mainmenu.ogg`, `level1.ogg` … `level5.ogg`. |

Both directly usable — WAV + OGG are bog-standard. Audio system maps
sound names to file paths. **Direct drop-in compatibility for both
directories.**

### `makelev/` — the editor side

- `colors.png` — the color → attribute legend (Repair = teal, Air =
  black, Water = blue, etc.). Embedded as tou2d's attribute legend too.
- `COLPICK.EXE`, `level converter.exe` — original tooling. Replaced
  with `tou2d_import_lev` / `tou2d_import_shp` CLIs.
- `Jungle.{jpg,tga,txt}`, `Junglep.jpg` — a worked source-asset bundle
  that compiles into `jungle.lev`. Useful for cross-checking the
  importer.

---

## 2. `.lev` file format (4 levels analyzed)

All four shipped `.lev`s share the same `TOU level file v1.4\r\n\x1a`
magic. Reverse-engineered from raw hex inspection of `minibase.lev`
(smallest of the four, easiest to map).

```
offset 0x00   "TOU level file v1.4\r\n\x1a"             21-byte ASCII magic + DOS-EOF
offset 0x15   <binary header — sizes, theme, options>   ~0x100 bytes; contains width/height u32,
                                                        author string at 0x1c, email at 0xa6,
                                                        theme name string ("the earth") at 0x132
offset ~0x3be ffd8 ff…                                  embedded JPEG (the level visual layer)
followed by   embedded TGA (24-bit uncompressed)        the attribute map
```

**Decoded fields** (confirmed at the offsets above):
- Magic (offset `0x00`, 21 bytes).
- Author string (offset `0x1c`, NUL-terminated).
- Email string (offset `0xa6`, NUL-terminated).
- Theme name (offset `0x132`, NUL-terminated).

**Container strategy** — treat `.lev` as a container; the importer
does:

1. Verify magic at `0x00`. Reject otherwise.
2. Seek to the first `0xff 0xd8` JPEG SOI marker; copy to
   `<output_dir>/foo.jpg`.
3. Seek past the JPEG EOI (`0xff 0xd9`); locate the TGA signature
   (`<width-le-u16><height-le-u16> 0x18`) immediately after. Copy to
   `<output_dir>/foo.tga`.
4. Extract the strings at known offsets for `theme` / `author` into a
   sidecar `foo.txt` in the `Normal.txt` `/KEY value` schema.

### What's STILL not decoded

The exact field layout between byte `0x14` and the start of the
embedded JPEG (`~0x3be`). **2026-05-31 round-2 RE: the `/KEY value`
blob layout is now FULLY mapped** by cross-referencing each level's
on-disk bytes against `makelev/<Stem>.txt`. Layout:

```
+0x0022..+0x00A2  /MAKER         char[128]  NUL-padded ASCII (Latin-1 ok)
+0x00A2..+0x0122  /EMAIL         char[128]  NUL-padded ASCII
+0x0122          /PARA           u8 bool   (00=no, 01=yes)
+0x0123          /CIVIL          u8        0..100
+0x0124          /BOMB           u8        0..100
+0x0125..+0x0128 /WATERC         u8[3]     R, G, B
+0x0128          /DISABLERUN     u8 bool
+0x0129          /GRAVITY        u8        ×10 (stored value 10 ⇔ 100 %)
+0x012A          /RESISTANCE     u8        ×10
+0x012B          /COLLDAMAGE     u8        ×10
+0x012C          /BOUNCING       u8        ×10
+0x012D          /AMBIENT        u8        ambient-sound id (0=none, 1=jungle, …)
+0x012E          /PARALLAXAT     u8
+0x012F          /GGLEVEL        u8 bool
+0x0130..+0x0140 /GGTHEME        char[16]  NUL-padded ASCII ("the earth", …)
+0x0140..+0x01B0 (reserved)      112 bytes  all-zero in every shipped level
+0x01B0          /GGSHAPE        u8 bool
+0x01B1          /REPAIR         u8
+0x01B2          /STUFFD         u8
+0x01B3          /SIGND          u8
+0x01B4..+0x01B6 (reserved)      2 bytes   zero in every shipped level
+0x01B6          /RANDOMSEED     u16 LE
+0x01B8..jpegStart (zero padding to JPEG #1)
```

Cross-check vs the 4 shipped levels (all parsed clean against their
makelev `.txt` sidecars):

| Field | Jungle | Desert | Minibase | Woods |
|---|---|---|---|---|
| /WATERC | 0,120,157 | 0,85,114 | 31,64,7 | 50,50,255 |
| /DISABLERUN | yes | no | no | no |
| /COLLDAMAGE | 50 | 100 | 100 | 100 |
| /BOUNCING | 50 | 100 | 100 | 100 |
| /AMBIENT | 1 | 0 | 0 | 0 |
| /PARALLAXAT | 2 | 2 | 4 | 4 |
| /GGTHEME | "the earth" (all) | | | |
| /GGSHAPE / /REPAIR / /STUFFD / /SIGND / /RANDOMSEED | yes, 20, 20, 20, 54321 (all) | | | |

**Practical impact**: `tou2d_import_lev` now writes a config.txt with
the parsed physics knobs (water color, gravity, resistance, collision
damage, bounce, parallax, ambient sound) so imported levels match the
original feel without manual editing.

### Importer status (`tou2d_import_lev`)

- **Decoded header** (2026-05-31 investigation): the binary header
  carries three `u32 LE` offsets — `jpegStart` at `0x16`, `jpegEnd` at
  `0x1A`, `section3Start` at `0x1E` — and an `author` string at `0x22`
  (32 bytes NUL-padded). The container exposes TWO JPEGs (visual +
  parallax) plus a `section3.bin` trailer.
- **JPEG #2 = parallax sky** (confirmed 2026-06-01): the second JPEG
  is a slow-scrolling sky / parallax background that the original game
  composites BEHIND the destructible terrain, visible through Air
  pixels. The `/PARALLAXAT` config field controls the depth (≥1) —
  higher values yield slower visible scroll. tou2d renders it via the
  Vulkan renderer's sky layer (B3); the parallax quad is sized at
  `level extent × parallaxat` so camera traversal walks `1/parallaxat`
  of the image's UV range, giving correct visible-speed parallax with
  no per-frame state updates. Jungle/desert/minibase = 2, Woods = 4.
- **`attribute.tga` extraction strategy** — the importer first looks
  for a sibling source TGA at `<install>/makelev/<Stem>.tga`. Only
  `jungle.lev` ships one in a vanilla install; desert / minibase /
  woods used to come up empty. **2026-05-31 fix**: a JPEG-derived
  fallback now reads `visual.jpg` and emits a 24-bit uncompressed TGA
  with a luminance threshold (Y ≥ 64 → Solid (white triple), else Air
  (black)). All 4 levels now produce a valid `attribute.tga` post-
  import.
- **Per-level config sidecar** is default values, not extracted from
  the binary blob. The `/KEY value` `/GRAVITY` etc. payload from
  `makelev/Normal.txt` lives in the un-decoded bytes between `0x22`
  and `jpegStart` (`~0x3BE` in `minibase.lev`); that's continuation
  work below.

### section3 — partial decode (2026-05-31, expanded round-2)

`section3.bin` is the original attribute map. Round-2 investigation
nailed the format down further but stopped short of a bit-perfect
decoder. What's confirmed:

```
offset 0x00         u32 LE record_count      (entity / point-of-interest count)
offset 0x04         record_count × 20-byte records:
                      u32 LE x        (image-pixel coord; in [0, image_w))
                      u32 LE y        (image-pixel coord; in [0, image_h))
                      12 bytes payload  (see "record payload" below)
after records       RLE attribute stream
final two bytes     0xFF 0xFF                (terminator)
```

**Record positions verified as in-Air spawn / POI sites** — every
shipped record falls on an Air pixel (RGB ≤ ~80 luminance) of the
visual JPG. desert (3 recs), jungle (1), minibase (6), woods (0).

**Record payload (12 bytes)** — partial decode. Common forms across
the shipped levels:
- `02 03 03 00 00 00 …` (desert ×3, minibase[1] / [3]) — looks like a
  "default spawn" template.
- `01 03 01 03 01 00 …` (jungle's only rec) — different mix of small ints.
- minibase carries variants `00 02 0e 03 …`, `01 01 01 03 01 01 …`,
  `02 01 03 01 …`, `02 00 03 01 01 …`.
- Bytes 5..11 are zero in every shipped record — high-byte padding.

The non-zero prefix looks team-or-type-encoded but we can't ground
the labels without TOU source. Best current guess: bytes 0–3 are a
team/kind/style triple and byte 4 is a sub-flag.

**RLE stream format (now confirmed)**:
- Pairs of `(value:u8, count:u8)` — value byte first.
- `count == 0` is the compact short-form for a 1-cell run (saves a byte
  vs encoding `(v, 1)` in the rare cases where the encoder needs to
  emit a single cell between longer runs).
- `(0xFF, 0xFF)` at end of stream is the terminator.
- Maximum-length runs of >255 cells are split into multiple pairs of
  the same value (the tail of jungle is 12+ `(0x67, 0xFF)` pairs
  followed by `(0x67, 0xF0)` and the terminator — totalling `12×255 +
  240 = 3300` cells of value `0x67`).

**Value-byte taxonomy**:
The high nibble appears to be a sub-type / flag, the low nibble the
primary attribute class. Observed byte values across all 4 levels are
exactly `{0x00..0x0B, 0x10..0x13, 0x20..0x27, 0x2C..0x2F, 0x60..0x67}`
plus `0xFF` for the terminator. Matches the `colors.png` legend's
roughly-30 attribute kinds (Air, Normal land, Brickwall, Repair
places per team, Water with direction & power 1/2, Air streams, etc.).
The low-nibble alignment with the legend's grouping is consistent
with: low 4 bits = base attribute class, high bits = variant (water
direction × power, repair-team index, etc.).

**Value 3 = Air confirmed**: in jungle the first 59 RLE pairs are
`(0x03, 0xFF)` × 32 + `(0x03, …)`, totalling 15045 cells of value 3.
The top 60 rows of the makelev TGA (top-down) are pure black (Air)
= 30 rows at 2× downsample × 500 = 15000 cells. Match is ≤0.3% off
on the leading air block — value 3 corresponds to the Air (transparent)
class.

**Cell count vs target grid**: decoded cell counts across all 4 levels
sit at ~98% of `(W/2)×(H/2)`:

| Level  | Cells | (W/2)×(H/2) | Markers (v,0) | Ratio |
|---|---|---|---|---|
| desert | 245233 | 247500 | 165 | 0.991 |
| jungle | 266910 | 272500 | 1062 | 0.979 |
| minibase | 137956 | 140000 | 153 | 0.985 |
| woods | 285648 | 293000 | 5673 | 0.975 |

This confirms section3 is at the **2× downsample** of the source TGA.
Rendering jungle row-major at `W=500` produces a clearly recognisable
jungle (air at top, vegetation in middle, water at bottom, purple
swamp at the very bottom) but with a consistent left-ward shear —
each "row" loses some cells relative to the previous. Rendering as
column-major top-down gives the same level rotated 90° CCW, also
with shear. Best structural alignment achieved is ~63%
(section3-value → TGA-color majority match in row-major top-down).

**What the decoder is still missing**: how to consume the
`(v, 0)` markers correctly so the row stride lands exactly on the
grid edge. Hypotheses tested and rejected:
- `(v, 0)` = row terminator (zero cells): produces 1063 rows for
  jungle when the target is 545 — wrong by ~2×.
- `(v, 0)` = single cell of v: gives the closest cell totals but
  leaves shear.
- `(v, 0)` = fill remaining cells in row with v: blows up cell totals
  by 4–5×.

The shear suggests there's either a separate per-row width carried by
the markers themselves, or the format has a column-major sub-stride I
haven't isolated.

**Why the decoder is parked**: the JPEG-derived fallback gets all 4
levels playable, and the section3 decode buys attribute fidelity
(water cells, repair pads embedded in the map, damaged-rubble states)
on top — a 2nd-order improvement over already-playable levels. For the
v1 level-picker workflow this is post-v1 polish.

### Continuation work for `.lev` RE

1. **Crack the section3 RLE row-stride mystery**. The cell stream
   sits at 98% of the 2× downsample of the source TGA. The 2% gap +
   shear correlates with the `(v, 0)` markers (1062 in jungle vs 545
   expected rows) but no row-end / fill-row / single-cell hypothesis
   makes the grid land exactly. Likely needs either TOU source or
   a richer hex-level comparison against jungle's known TGA. Not
   blocking: JPEG fallback gives playable levels.
2. **Map record payload bytes 0..4 to a real schema**. Spawn-position
   bytes look like a (team-id, kind, sub-kind, ...) tuple but the
   ground truth requires either `toudoc_makelev.htm`'s entity-counts
   tables or a controlled experiment (build a known-content `.lev`
   via the original editor and diff).
3. **Locate the `/RAINS`, `/PARALLAXSTREAM`, `/DESTROY`, `/TURRETD`
   fields** — `Normal.txt` documents more keys than we mapped to the
   binary. They might live in the 112-byte zero block at 0x140..0x1B0
   when set by non-default editors. None of the 4 shipped originals
   set them, so we have no positive samples.

---

## 3. `.SHP` file format (9 ships analyzed, fully decoded)

`.SHP` is the original game's ship-sprite container. **The body
decoder is fully landed** as of M4.7c/d (committed in
`examples/tou2d/ShpHeader.hpp` + `examples/tou2d/ShpBody.hpp`). This
section captures the RE journey + the final layout so future work
doesn't re-derive.

### 3.1 Header layout

Observed in `BATM.SHP`:

```
offset 0x00   0x00                                       version / padding byte
offset 0x01   "Batman ship\0"                            display name, NUL-terminated
offset 0x0d   <stats blob>                               turn-rate, thrust, mass, hp, hitbox
offset ~0x40  <sprite frames>                            32 rotation frames, each W×H pixels
                                                        × 3 bytes/pixel
```

The exact header-body boundary varies per-ship — header length =
`592 + name_length` empirically (see § 3.3 file-size table). Header
ends at the byte before the per-pixel triplet stream begins.

### 3.2 Stat blob — partial decode

Header parser (`examples/tou2d/ShpHeader.hpp`) extracts **frame
dimensions, rotation count, and max-HP** from the bytes around the
per-ship anchor pattern `WW 00 HH 00 18 20` (frame-width LE-u16,
frame-height LE-u16, byte `0x18` = 24 rotation steps, trailing
invariant `0x20`). The anchor sits at a per-ship-variable offset
(`anchorOffset` in `ParsedHeader`) — the bytes between the stat-extra
block and the anchor differ in length per ship, so search-based
location is required.

Empirically pinned across all 9 stock ships:

| File | Display name in file | Frame | rot | max-HP | stat triplet | stat extra (hex) |
|---|---|---|---|---|---|---|
| PERH | "Butterfly" | 26×26 | 24 | **20** | `02 02 01` | `6e 55 14 14` |
| TIEF | "Imperium Tie Fighter" | 26×26 | 24 | 50 | `02 02 02` | `32 46 1c 32` |
| BATM | "Batman ship" | 28×28 | 24 | 50 | `03 03 05` | `41 37 1a 32` |
| BEE2 | "B2 Stealth fighter" | 28×28 | 24 | 50 | `0a 01 04` | `4b 41 17 32` |
| PERU | "Basic TOU ship" | 24×24 | 24 | 50 | `04 01 03` | `32 32 1e 32` |
| SPED | "Speedie" | 22×22 | 24 | **40** | `04 01 03` | `5a 32 1a 28` |
| DEST | "Destroyer" | 30×30 | 24 | 50 | `06 02 06` | `23 23 25 32` |
| FLYY | "Fly" | 32×32 | 24 | 50 | `01 01 03` | `32 28 22 32` |
| XWIN | "X-Wing fighter" | 32×32 | 24 | 50 | `01 02 04` | `46 3c 19 32` |

**Decoded invariants**:
- `W == H` in all 9 files (square sprite frames).
- `rotation_count_header = 24 (0x18)` in all 9 files. **But the body
  actually contains 32 rotations** (see § 3.3) — the `0x18` field is
  NOT the body's frame count; best guess is some gameplay constant
  ("core" rotations for collision normals?). Not consumed by the
  decoder.
- `statExtra[3]` varies (0x14 / 0x28 / 0x32) and tracks weakness:
  Butterfly (PERH) is a 20-HP glass cannon; Speedie (SPED) is a 40-HP
  fast frame; everything else maxes at 50.

**What's still TBD**:
- **First three bytes of `statExtra`** — likely physics coefficients
  (mass / drag / turn-acceleration). They don't obviously correlate
  with the manual's Strength/Thrusters/Turning numbers. Cross-checking
  against in-game behavior would isolate which is which; out of scope
  for the importer.
- **Per-ship marker region** (bytes between `statExtra` and the W/H/rot
  anchor): 8 ships start with `?? 01 02 01 05 04`; XWIN's variant is
  `01 01 02 01 02 03`. Role unknown — possibly section marker,
  possibly per-ship animation timing, possibly version.
- **The ~500–550 bytes between the W/H anchor and the body start** —
  per-rotation offset table? Animation timing? Weapon hardpoints?
  Renderer doesn't need them; body is fixed-stride.

### 3.3 Body layout — fully decoded

Body layout (M4.7c, landed):

```
file_size = header_bytes + 32 * 3 * frame_w * frame_h
body_start = file_size - 32 * 3 * frame_w * frame_h
```

Per-ship header_bytes = `592 + name_length`:

| File | Size | Frame | 32×3×W×H | Header | Name length |
|---|---|---|---|---|---|
| FLYY | 98899 | 32×32 | 98304 | 595 | "Fly" (3) |
| SPED | 47063 | 22×22 | 46464 | 599 | "Speedie" (7) |
| DEST | 87001 | 30×30 | 86400 | 601 | "Destroyer" (9) |
| PERH | 65497 | 26×26 | 64896 | 601 | "Butterfly" (9) |
| BATM | 75867 | 28×28 | 75264 | 603 | "Batman ship" (11) |
| PERU | 55902 | 24×24 | 55296 | 606 | "Basic TOU ship" (14) |
| XWIN | 98910 | 32×32 | 98304 | 606 | "X-Wing fighter" (14) |
| BEE2 | 75874 | 28×28 | 75264 | 610 | "B2 Stealth fighter" (18) |
| TIEF | 65508 | 26×26 | 64896 | 612 | "Imperium Tie Fighter" (20) |

**Frame layout** — each rotation frame = `frame_w * frame_h` pixels,
3 bytes per pixel, interleaved triplet:

```
pixel(x, y) = body[ rotation*frame_size + (y*w + x)*3 .. +3 ]
            = (b0, b1, b2)
```

### 3.4 Color model — three intensity channels (M4.7d)

The three bytes per pixel are **NOT three palette indices** into the
VGA palette. They are three independent **intensity channels (0..255)**,
one per shaded region:

- `b0` = hull intensity (cross-brackets, structural elements)
- `b1` = team intensity (wings / faction-color region — different team
  = different color, the original game's per-team recolor mechanism)
- `b2` = cockpit intensity (cockpit-sphere / center detail)

Final pixel = additive blend of
`hull_color * b0/255 + team_color * b1/255 + cockpit_color * b2/255`
(per-channel saturating to 255), with alpha = 1 when any channel is
non-zero.

**Verification path** (dump TIEF frame 31):
- `b1` has a smooth 0..255 gradient in two symmetric clusters (the
  wings).
- `b2` has a smooth 0..255 gradient in the central cockpit blob.
- `b0` has small values along the structural T-bar.

Exactly the three overlaid shaded regions the original game ships
display.

### 3.5 Frame indexing + sentinel centering (M4.7d)

Body bytes are stored with the visible ship wrapped around the (0, 0)
origin of the frame; each frame (0..30) embeds a single sentinel pixel
at a deterministic position that marks the wrap origin:

```
flat_position(N) = W * H - 6 * (31 - N)
```

The `+6` step matches a fixed 3-pixel metadata trailer that every frame
carries at the sentinel position:

| Offset | (b0, b1, b2) | Meaning |
|---|---|---|
| +0 | (0, 0, 2) | magic marker |
| +2 | (0, 24, 0) | rotation count (0x18, mirrors header `anchor[4]`) |
| +4 | (W, 0, W) | frame width |

For frame 31 the formula evaluates to `W*H` (one past the last pixel),
which is the encoder saying "no sentinel — this frame is already
centered, render straight through". Verified across all 9 stock ships.

**Centering recipe** (rotations 0..30):

1. Decode `(sx, sy)` from the sentinel formula.
2. Primary toroidal shift `(sx, sy + 1)`:
   `post(x, y) = pre((x + sx) % W, (y + sy + 1) % H)`. Using `sy + 1`
   puts the sentinel row at the BOTTOM (row H-1) rather than the top.
3. Secondary horizontal shift: top half (post-shift rows
   `0 .. H - sy - 2`) gets `+6` columns. Empirically dialled across
   all 9 stock ships.
4. Trailer pixels at post-shift `(0, H-1)`, `(2, H-1)`, `(4, H-1)`
   cleared to transparent.

### 3.6 Rotation → heading mapping

32 rotations × 11.25° starts at "ship facing down" and proceeds
counter-clockwise (user-confirmed hypothesis, matches all 9 ships).

The runtime sprite compositor (`SpriteCompositor.hpp`) maps engine
heading → frame index as: frame 0 = ship facing down, `θ = π` in the
engine's "+Y is forward" convention. Frame index =
`round((θ - π) / (2π/32)) mod 32`.

### 3.7 Code landed for `.SHP` RE

- `examples/tou2d/PalCol.hpp` — VGA palette loader (256×3 bytes).
- `examples/tou2d/TgaWriter.hpp` — TGA emitter for debug visualizers.
- `examples/tou2d/ShpHeader.hpp` — `parseHeader()` + `ParsedHeader`
  with anchor-search, stat-extra extraction, max-HP.
- `examples/tou2d/ShpBody.hpp` — `parseBody()` + `ParsedBody` +
  `primarySentinel(W, H, N) -> optional<Sentinel>` +
  `compositeRotation` (legacy palette-index inspection) +
  `compositeRotationCentered` (the full M4.7d recipe in a single pass).
- `examples/tou2d/SpriteAtlas.hpp` — reads a `.SHP` from disk, decodes
  via the above, composites 32 RGBA frames at the SHP's native
  `frameWidth × frameHeight`, ready for the runtime compositor.
- `examples/tou2d/scripts/decode_sprite.py` — definitive Python
  decoder / visualizer; emits per-ship 32-rotation montage PNGs.
- `examples/tou2d/scripts/decode_spike.py` — historical hypothesis
  cycle (Variants A/B/C/D), kept for the next person doing similar
  RE work as a record of what doesn't work.
- `tests/tou2d_pal_col_test.cpp`, `tests/tou2d_shp_header_test.cpp`,
  `tests/tou2d_shp_body_test.cpp` — pinning tests for the decoders;
  11 cases on the body decoder alone (sentinel formula across
  PERH/FLYY/SPED, frame-31 no-shift special case, trailer suppression,
  blend-color math, primary+secondary shift composition).
- `examples/tou2d/tou2d_import_shp.cpp` — when `--palette` is given,
  emits `palette.tga` (128×128 swatch) and `rotations.tga`
  (frame_w × 32 wide, frame_h tall composite sheet using
  `compositeRotationCentered`).

### 3.8 Body-decode RE journey (kept for context)

The decoder didn't come together in one shot — pattern-matching alone
took several false starts before the file-size-arithmetic breakthrough.
Pinning the journey so future similar work has a record of what doesn't
work + what eventually did.

**Variant trials** (`scripts/decode_spike.py`) ran four decode
hypotheses against `PERH.SHP`'s body:

| Variant | Rule | PERH result |
|---|---|---|
| A | row = `[skip][run][color]...` until `0x00` terminator | 0 opaque (failed) |
| B | row = `[N_runs][triplet * N_runs]` | 0 opaque (failed) |
| C | classic RLE: `[count][color]`, row wraps at width | 0 opaque (failed) |
| D | pure `[skip][run][color]` triplet stream, no delim | **584 opaque / 676 in frame 0** |

A–C all failed the same way: the body's leading 269 zero bytes got
consumed as fake-empty frames (each `0x00` interpreted as some kind of
terminator), so the decoder never reached real pixel data.

Variant D produced visible structure but didn't divide cleanly — pixel
cursor sat at 592,705 after exhausting all triplets, which doesn't
divide by `676 × 24`. So D wasn't quite right either: the encoding
isn't a pure flat raster of fixed-size frames as Variant D assumed.

**The actual breakthrough** combined two threads:
1. A user-side parallel RE pass (untracked
   `tou_shp_mirror_pairs.py`) hypothesizing 32 frames sliced from end-
   of-file rather than start.
2. A cross-ship file-size audit:
   `file_size = header_bytes + 32 * 3 * frame_w * frame_h` — exact
   for ALL 9 ships once you allow `header_bytes` to vary with name
   length.

This established that the body is a fixed-stride raw raster (32
rotations × W*H pixels × 3 bytes/pixel), no compression involved. The
Variant-D triplet bytes ARE the sprite data; they're just not
[skip][run][color] — they're the three intensity channels of § 3.4.

**Lesson for the next opaque-format RE**: pure pattern matching is
insufficient. The fix here was arithmetic on the file-size table once
multiple ships were available — exact-divisibility constraints pin
the layout down faster than any single-file hex inspection.

---

## 4. Original gameplay reference

Distilled from `toudoc_controls.htm`, `toudoc_weapons.htm`,
`toudoc_ships.htm`, `toudoc_features.htm`. Recording here so future
batches don't re-read the HTML.

### 4.1 Controls (defaults — fully rebindable in the original)

| Action | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| Thrust | ↑ | W | I | Numpad 5 |
| Back | ↓ | S | K | Numpad 2 |
| Turn left | ← | A | J | Numpad 1 |
| Turn right | → | D | L | Numpad 3 |
| Basic weapon | RShift | Tab | B | Numpad 7 |
| Special weapon | RCtrl | Q | V | Numpad 8 |
| Menu / launch-all | `/` | `1` | N | Numpad 9 |

**Emergency teleport** (per `toudoc_controls.htm`): holding both
turn-left + turn-right simultaneously for ≥ 1 s triggers an emergency
teleport that costs energy. Preserve this — it's part of the game's
feel and trivial to wire. (Landed in tou2d's `InputSystem`.)

### 4.2 Ship stats (from `toudoc_ships.htm`)

Manual lists the 9 stock ships in the original's "Strength / Thrusters
/ Turning" UI. Note these are MANUAL values — they don't necessarily
correspond bit-for-bit to the in-binary stat blob (§ 3.2), but they
give the design intent.

| Ship (manual name) | Strength | Thrusters | Turning | Notes |
|---|---|---|---|---|
| Basic ship | 3 | 3 | 3 | baseline |
| Batman | 2.5 | 4 | 3.5 | |
| B2 Stealth | 2 | 5 | 4 | |
| Micro Speedie | 1.5 | 7 | 3 | heavy basic weapons |
| X Wing | 2.5 | 4.5 | 4 | |
| Tie Fighter | 2.5 | 5 | 4.5 | |
| Bee | 1 | 10 | 6.5 | heavy basic weapon |
| Fly | 4 | 3 | 2 | |
| Destroyer | 6 | 1.5 | 1 | |

### 4.3 Weapons — original full list

The original has 50+ weapons. tou2d ships 10 mechanically-distinct
ones in v1 (chosen so each fires on a different system path — proves
engine breadth, not just the projectile system). The remaining ~40
from the original docs (rockets, mines, homers, persuadertrons, …)
are mechanical variants of the v1 ten and would land in expansion.

v1 ten:

| Weapon | Mechanic class | System touched |
|---|---|---|
| Shotgun | Hitscan cone | Projectile + raycast |
| Dumbfire | Forward unguided projectile | Projectile |
| Organic waste | Spawns growing physical entities | Spawner + collision-merge |
| Troopers | Spawns AI ground units | Spawner + path-finding |
| Turbo boost | Temporary stat buff on caster | Buff system |
| Collapser | Hitless terrain-trigger weapon | Terrain mutation only |
| Teleporter | Translates caster | Direct transform mutation |
| Kicker | Impulse-only, low damage | Velocity application |
| Nuclear barrel | Persistent triggerable entity | Spawner + delayed event |
| Brickwall | Hardens existing terrain | Terrain mutation (tile property change) |

### 4.4 Level config — `/KEY value` schema

The original `/KEY <value>` text format in `makelev/Normal.txt` is so
simple it's worth keeping verbatim as the **runtime level config
format** for tou2d (with the binary `.lev` importer producing
equivalent files).

Day-one honored knobs:
- `/GRAVITY` — vertical acceleration, percent of standard.
- `/RESISTANCE` — drag coefficient.
- `/COLLDAMAGE` — ship-vs-terrain damage scale.
- `/BOUNCING` — bounce coefficient on hard hits.
- `/WATERC` — water current scalar.
- `/AMBIENT` — ambient light level.
- `/PARA` — parallax background scale.
- `/CIVIL` — civilian-density knob.
- `/BOMB` — bomb spawn density.
- `/DISABLERUN` — disables running ships (gameplay tweak).

GG-only knobs (gate the procedural-generation path):
- `/GGLEVEL`
- `/GGTHEME`
- `/STUFFD`
- `/SIGND`
- `/RANDOMSEED`

The `colors.png` attribute legend (in `makelev/`):

| Color | Attribute |
|---|---|
| Black | Air (no tile) |
| Teal | Repair pad |
| Blue | Water |
| White | Solid ground (standard) |
| (additional colors per `colors.png` legend) | (various terrain attributes) |

Importer reads `colors.png` directly to build the color → attribute
lookup table; no hardcoding in C++.

---

## 5. Compatibility tiers (current state)

Ordered cheapest-to-deliver first. The tier model is what the importer
infrastructure follows.

### Tier 0 — Asset-format pass-through (LANDED)

Drop-in support for the open formats the original already used:
- 24-bit uncompressed `.tga` attribute maps. `stb_image` load; pixel
  color → tile attribute per the `colors.png` legend.
- `.jpg` visual layers, parallax backgrounds, GG-theme textures.
  `stb_image` load, GPU atlas upload.
- `.wav` / `.ogg` audio. miniaudio plays directly.
- `info.txt`, `Normal.txt`-style `/KEY value` configs. Hand-rolled
  parser.
- GG theme directories with their tagged naming (`s1.tga`, `t1.jpg`,
  `l1.tga`, etc.).

### Tier 1 — Binary `.lev` import (LANDED, partial; see § 2)

`tou2d_import_lev <path/to/foo.lev> <output_dir>`. Extracts JPEG +
TGA + default-config sidecar. The 4 shipped `.lev`s round-trip JPEG
extraction cleanly; TGA extraction is flaky (per § 2 last-known
state); per-level config blob is not yet decoded.

### Tier 2 — Binary `.SHP` import (LANDED)

`tou2d_import_shp <path/to/foo.SHP> <output_dir>`. Per § 3, fully
decoded across all 9 shipped ships; produces 32-rotation RGBA atlas
with proper centering + per-team color tinting.

### Tier 3 — Native source-asset workflow (LANDED)

Skip the binary containers entirely. User edits `<level>.jpg`
(visual) + `<level>.tga` (attribute map per `colors.png`) +
`<level>.txt` (`/KEY value` schema) in Photoshop / GIMP. Drops all
three into `assets/levels/<level>/`. Engine picks them up at runtime.

This is the workflow the original's manual already documents. tou2d
just doesn't require the `makelev.exe` compile step.

### Explicit non-goals

- `options.cfg` — opaque, replaced with `settings.dat`.
- `.gfx` / `.col` / `.dat` / `.tau` in `data/` — proprietary
  containers, not decoded (palette `.col` IS decoded — see § 1).
- `fmod.dll` / `ijl10.dll` — replaced with miniaudio + stb_image.
- Save-game compatibility — there are no save games in the original.

---

## 6. Continuation work

Open RE / importer threads, in rough priority order:

1. **`.lev` TGA extraction flakiness** (§ 2). Reproduce the
   "attribute.tga not created" symptom on `jungle.lev`; instrument
   the TGA-locator path to log exactly where it bails. The TGA
   header sniff (signature at byte-after-JPEG-EOI) is the suspected
   weak point.
2. **Decode `.lev` `/KEY value` config blob** (§ 2). Adds physics-knob
   fidelity to imported levels without requiring hand-edit of the
   sidecar `.txt`. Probable approach: visual hex viewer
   highlighting printable-ASCII runs preceded by `/` between offset
   `0x15` and the JPEG SOI.
3. **First three bytes of `.SHP` `statExtra`** (§ 3.2). Likely
   mass / drag / turn-acceleration physics coefficients. Map by
   cross-checking in-game ship behavior against the values.
4. **Per-ship `.SHP` marker region** (§ 3.2). 8 ships start with
   `?? 01 02 01 05 04`; XWIN has `01 01 02 01 02 03`. Role unknown;
   minor priority since the renderer doesn't need it.
5. **Sentinel-row strip disposition** (§ 3.5). The trailer-row strip
   lands "above" the ship in some rotations and "below" in others;
   visual inspection at engine-side render scale will tell us whether
   to keep, flip, or hide it.

None of these block current tou2d functionality. Each is a focused
follow-up that closes a specific compatibility gap.
