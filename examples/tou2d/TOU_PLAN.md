# TOU 2D — threadmaxx Adaptation Plan

A planning document for the `examples/tou2d/` proof-of-engine project. Reads the ChatGPT design notes at `../../tou_threadmaxx_design_notes.md` together with a direct inspection of the original `TOU/` install (v1.0, GigaMess, 2002), and lands on an actionable scope that:

1. Proves threadmaxx as a 2D game backend (sprite batching, particles, destructible terrain, local multiplayer).
2. Is **partially compatible** with the original TOU on the asset side — players can drop their hand-drawn `.tga` attribute maps, GG theme directories, ship sprites, and `.txt` configs into the new project's asset path, and the importer turns them into in-engine entities. Binary `.lev` and `.SHP` containers are imported by a separate converter step, not at runtime.

The plan is intentionally implementation-light. It picks decisions but stops short of pinning per-file APIs — those land in batch-by-batch follow-ups (see § "Milestone breakdown").

---

## 1. Scope decisions (locked)

These resolve every "open question" in the ChatGPT notes' final section so the rest of this document and every subsequent batch has a single source of truth.

| Question | Decision | Rationale |
|---|---|---|
| Camera | **Shared dynamic camera** that frames all live ships, scales zoom to fit. Split-screen deferred to a stretch milestone. | Cheapest renderer proof that still exercises camera math; matches original "all on one screen" feel. |
| Terrain | **Tile-grid attribute map** (one byte per cell; cells ≈ 4×4 source pixels). A separate JPG/TGA visual layer is rendered as a sprite over the tile colliders. | Matches the original's "TGA attribute + JPG visual" split exactly — direct asset compatibility. Tile resolution chosen so a 2034×2000-px original level fits in ≈ 508×500 tiles ≈ 250 k entities-worth of state (cheap). |
| Weapons for v1 | **10 weapons**: shotgun, dumbfire, organic-waste, troopers, turbo-boost, collapser, teleporter, kicker, nuclear-barrel, brickwall. Picks the most mechanically-distinct subset from the original's 50+. | Each fires on a different system path (projectile / spawner / movement-modifier / wall-mutator / area-effect / persistent-entity) — proves the engine's breadth, not just the projectile system. |
| Movement | **Faithful inertia + gravity**, gravity configurable per-level (matches original `/GRAVITY` percent), no clamp on terminal velocity. | The original's twitchy-arcade feel comes entirely from inertia retention; flattening it kills the game's identity. |
| Bots | **On by default**, fill empty slots to 4 ships. | Single-machine local-MP cold-start without humans is the engine-stress mode the design notes call out. |
| Map editor | **Out of scope for v1**. Compatible with original `makelev/`-style `.tga + .txt` workflow — the user keeps editing in Photoshop / GIMP, the engine imports. | The original's editor was already external; we get editor support "for free" by being import-compatible. |
| Fixed-step | **Yes**. `Engine::run()` at fixed dt (default 1/60 s). | Required for determinism + replay; threadmaxx is built for this. |
| Replay | **Stretch**. Input recording lands in milestone 5 if there's room. | Not needed for the engine proof; nice-to-have for debugging. |

---

## 2. Observed original-TOU asset inventory

This is what the install directory at `../../TOU/` actually contains. Pinning it here so future batches don't re-derive.

### Top-level files
- `TOU.exe` (512 KB) — the original Windows binary. Treat as opaque; we don't disassemble it. Use only as a runtime oracle to compare gameplay feel.
- `options.cfg` (≈ 6 KB) — opaque binary blob (mostly zero-padded with embedded XOR-or-compressed payload). Not worth supporting; the new project uses its own config format.
- `readme.txt` / `file_id.diz` / `toudoc*.htm` — documentation. The HTML pages are the authoritative source for weapon behaviors, ship stats, and control bindings (see § 3 below).
- `fmod.dll`, `ijl10.dll` — third-party audio (FMOD) and JPEG (Intel JPEG Library) DLLs. Not redistributable; we replace with our own backends (likely `miniaudio` + `stb_image`, but library choice is deferred).

### `levels/` — 4 binary level containers
| File | Size | Visible header |
|---|---|---|
| `desert.lev` | 161 KB | `TOU level file v1.4\r\n\x1a` |
| `jungle.lev` | 157 KB | same magic, author "Hannu Kankaanpää" |
| `minibase.lev` | 83 KB | same magic |
| `woods.lev` | 200 KB | same magic |

Format reverse-engineering (from raw hex inspection of `minibase.lev`):

```
offset 0x00   "TOU level file v1.4\r\n\x1a"             21-byte ASCII magic + DOS-EOF
offset 0x15   <binary header — sizes, theme, options>   ~0x100 bytes; contains width/height u32,
                                                        author string at 0x1c, email at 0xa6,
                                                        theme name string ("the earth") at 0x132
offset ~0x3be ffd8 ff…                                  embedded JPEG (the level visual layer)
followed by   embedded TGA (24-bit uncompressed)        the attribute map
```

The exact field layout past byte 0x14 isn't fully decoded. **Strategy**: treat `.lev` as a container; a small parser does (a) seek to `0xffd8` JPEG SOI to extract the visual; (b) seek to the TGA signature (`<width-le-u16><height-le-u16>0x18` at offset ≥ JPEG-end) to extract the attribute map; (c) extract the strings at the known offsets for theme / author / config. Anything we can't decode falls back to per-level overrides in a sidecar `.toml` (see compatibility tiers below).

### `ships/` — 9 ship designs
| File | Size | Ship name (from leading string) |
|---|---|---|
| `BATM.SHP` | 75 KB | Batman ship |
| `BEE2.SHP` | 75 KB | Bee |
| `DEST.SHP` | 87 KB | Destroyer |
| `FLYY.SHP` | 98 KB | Fly |
| `PERH.SHP` | 65 KB | Perh (basic ship) |
| `PERU.SHP` | 55 KB | Peru (B2 Stealth) |
| `SPED.SHP` | 47 KB | Micro Speedie |
| `TIEF.SHP` | 65 KB | Tie Fighter |
| `XWIN.SHP` | 98 KB | X Wing |

Format: opaque binary. Header observed in `BATM.SHP`:

```
offset 0x00   0x00                                       version / padding byte
offset 0x01   "Batman ship\0"                            display name, NUL-terminated
offset 0x0d   <stats blob>                               turn-rate, thrust, mass, hp, hitbox
offset ~0x40  <sprite frames>                            small palettized bitmaps for each
                                                        rotation step (the doc says ships use
                                                        the `Pal.col` palette in `data/`)
```

Per `toudoc_ships.htm`, gameplay stats are visible in the original's "Strength / Thrusters / Turning" UI:

| Ship | Strength | Thrusters | Turning | Notes |
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

These stats come from the manual, **not** from a fully-decoded `.SHP` — the in-binary stats blob should round-trip identically once the field layout is decoded, but the table here is the design-doc authority during bring-up.

### `data/` — palettes, fonts, menu graphics
| File | Purpose |
|---|---|
| `Pal.col`, `SHIPAL.COL` | 8-bit palettes (likely 256×3 bytes). Used by the original's palettized rendering. |
| `f_large.tga`, `f_med.tga`, `f_mini.tga`, `f_tiny5d.tga` | Bitmap fonts. |
| `menu3d.jpg`, `splay.jpg`, `sanim2.jpg` | Menu background art. |
| `all3.gfx`, `explode.gfx` | Sprite atlases (proprietary `.gfx` format — opaque). |
| `NAMES.DAT` | Probably bot / ship random-name pool. |
| `loadtime.dat`, `taulu2.tau` | Opaque. Unused. |

**Strategy**: redraw fonts and menu art from scratch (these are visual identity, not gameplay-load-bearing). The palettes are interesting only if we want pixel-perfect re-rendering of the original ship sprites — defer to a stretch goal.

### `ggstuff/` — 7 procedural-level (GG) themes
Six theme directories: `happyland`, `rocky mountains`, `the beach`, `the diablo`, `the earth`, `winter`. Each has the standard tagged-naming asset set documented in `toudoc_makelev.htm`:

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

**This is the easiest compatibility win.** The directory layout is filesystem-native, the formats are open (TGA + JPEG), and the `info.txt` is human-readable. Drop a TOU theme directory into `examples/tou2d/themes/` and the engine should pick it up with zero conversion.

### `sfx/` and `music/`
| Dir | Contents |
|---|---|
| `sfx/` | ≈ 100 `.wav` files, 8-character DOS-style names, naming convention `<cat>_<intensity>.wav` — `r` (random?), `m` (medium), `h` (heavy), `l` (low). |
| `music/` | 6 `.ogg` files: `mainmenu.ogg`, `level1.ogg` … `level5.ogg`. |

Both directly usable — WAV + OGG are bog-standard formats. The audio system just needs to map sound names to file paths. **Direct drop-in compatibility for both directories.**

### `makelev/` — the editor side
- `colors.png` — the color → attribute legend (Repair = teal, Air = black, Water = blue, etc.). We embed this as our attribute legend too.
- `COLPICK.EXE`, `level converter.exe` — original tooling. We replace with our own `tou2d_import` CLI (see § 6).
- `Jungle.{jpg,tga,txt}`, `Junglep.jpg` — a worked example of the source-asset bundle that compiles into `jungle.lev`.

---

## 3. Original gameplay specifics worth pinning before code

Distilled from `toudoc_controls.htm`, `toudoc_weapons.htm`, `toudoc_ships.htm`, `toudoc_features.htm`. Recording here so future batches don't re-read the HTML.

### Controls (defaults — fully rebindable in the original)
| Action | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| Thrust | ↑ | W | I | Numpad 5 |
| Back | ↓ | S | K | Numpad 2 |
| Turn left | ← | A | J | Numpad 1 |
| Turn right | → | D | L | Numpad 3 |
| Basic weapon | RShift | Tab | B | Numpad 7 |
| Special weapon | RCtrl | Q | V | Numpad 8 |
| Menu / launch-all | `/` | `1` | N | Numpad 9 |

**Notable mechanic** (also from controls.htm): holding both turn-left + turn-right simultaneously for ≥ 1 s triggers an "emergency teleport" that costs energy. Preserve this — it's part of the game's feel and trivial to wire.

### Weapons — v1 subset (10 picks)
| Weapon | Mechanic class | System touched |
|---|---|---|
| Shotgun | Hitscan cone | Projectile system + raycast |
| Dumbfire | Forward unguided projectile | Projectile system |
| Organic waste | Spawns growing physical entities | Spawner + collision-merge |
| Troopers | Spawns AI ground units | Spawner + path-finding |
| Turbo boost | Temporary stat buff on caster | Buff system |
| Collapser | Hitless terrain-trigger weapon | Terrain mutation only |
| Teleporter | Translates caster | Direct transform mutation |
| Kicker | Impulse-only, low damage | Velocity application |
| Nuclear barrel | Persistent triggerable entity | Spawner + delayed event |
| Brickwall | Hardens existing terrain | Terrain mutation (tile property change) |

These ten exhibit ten distinct touch-paths through the engine. The remaining 40+ from the original docs (rockets, mines, homers, persuadertrons, …) are mechanical variants of these ten and land in milestone 4.

### Ship stats → user components
The 9 ships from `toudoc_ships.htm` map cleanly to a single user component:

```cpp
struct ShipKind {
    std::array<char, 16> displayName;
    float strength;     // max HP (1-10 scale * 20 = 20-200 HP)
    float thrustForce;  // 1-10 scale
    float turnRate;     // 1-10 scale (rad/sec * scale/10)
    uint32_t spriteId;  // index into sprite atlas
};
```

`ShipKind` is a small, immutable archetype description — stored in a `ResourceRegistry` slot (per-ship-kind one entry), not per-entity. The per-entity `Ship` user component just carries `shipKindId`, current HP, current weapon, etc.

### Level config — direct port
The original `/KEY <value>` text format in `makelev/Normal.txt` is so simple it's worth keeping verbatim as the **runtime level config format** for the new project (with the binary `.lev` blob importer producing equivalent files). Knobs we honor on day one: `/GRAVITY`, `/RESISTANCE`, `/COLLDAMAGE`, `/BOUNCING`, `/WATERC`, `/AMBIENT`, `/PARA`, `/CIVIL`, `/BOMB`, `/DISABLERUN`. The GG-only knobs (`/GGLEVEL`, `/GGTHEME`, `/STUFFD`, `/SIGND`, `/RANDOMSEED`) gate the procedural-generation path.

---

## 4. threadmaxx mapping

The whole reason to do this project is to exercise the engine. Mapping here so design choices follow that goal.

### 4.1 New built-in components — **none planned**

Every game-side concept rides on `UserComponent<T>` (`include/threadmaxx/UserComponent.hpp`). The engine's built-in `Transform / Velocity / Acceleration / Health` already cover ship state. New user components, registered in `Game::onSetup`:

| Component | Purpose | Lifetime |
|---|---|---|
| `Ship` | shipKindId, currentWeapon, weaponPower, lives, score | persistent across rounds |
| `Bullet` | weaponKind, damage, sourcePlayerId, timeToLive, gravityScale | very short (1–3 s typical) |
| `Particle` | sourceEffectId, ttl, color packed u32, sizeStart, sizeEnd | < 1 s |
| `TerrainBlock` | tileX, tileY, attribute, currentHP | persistent, mutated under fire |
| `WeaponSpawned` (waste / troopers / barrels / mines) | weaponKind, ownerId, state | seconds to minutes |
| `PlayerInput` | thrust/back/left/right/fireA/fireB/menu bits, deviceId | 1 tick (refresh in preStep) |
| `LocalPlayer` | playerSlot (0-3), inputBinding | persistent |
| `BotAI` | aiState enum, currentTarget EntityHandle, retreatUntil tick | persistent |
| `SpriteRef` | atlasId, frameId, rotationFrames, currentFrame | persistent |

Tag-only categories (`addTag`/`removeTag`, no dense storage): `OnRepairPad`, `IsDead`, `IsBoosted`, `IsKickerStunned`.

### 4.2 System inventory + waves

Reads/writes declared per system so the wave scheduler can group them. Listed in registration order; the wave packer will fan them out.

```
preStep         InputSystem (drains keyboard/joystick → PlayerInput)
                BotAISystem  (writes PlayerInput for bot ships)
wave 1          MovementSystem        R: PlayerInput,Transform,Velocity  W: Velocity,Acceleration
                GravitySystem         R: Transform,Velocity              W: Velocity
                WeaponFireSystem      R: PlayerInput,Ship,Transform      W: -- (emits Bullet spawns via CB)
wave 2          ProjectileSystem      R: Bullet,Transform                W: Transform,Velocity
                ParticleSystem        R: Particle,Transform              W: Transform,UserData
                WeaponSpawnedSystem   R: WeaponSpawned,Transform         W: state via UserData
wave 3          CollisionSystem       R: Transform,UserData              W: -- (emits damage / terrain events)
                                      reads: spatial hash
                TerrainDamageSystem   R: -- (events)                     W: TerrainBlock (via CB.setUserData)
                DamageSystem          R: -- (events)                     W: Health (built-in)
wave 4          RespawnSystem         R: Health,Ship                     W: Ship (lives), spawns new entities
                ScoreSystem           R: Ship                            W: Ship (score)
postStep        CameraSystem          R: Transform                       W: -- (mutates RenderFrameBuilder)
                HudSystem             R: Ship                            W: -- (RenderFrameBuilder)
                AudioTriggerSystem    R: -- (events)                     W: -- (drives miniaudio)
                buildRenderFrame      hook on every render-relevant system
```

The wave count is illustrative — the scheduler computes the actual packing from `reads()` / `writes()`. The point is that conflict-free systems (e.g. Movement and Gravity both write Velocity → same wave OR sequential; Particles + Projectiles touch disjoint user-component sets → same wave) get parallelized for free.

### 4.3 Spatial index

A single `SpatialHash<EntityHandle>` rebuilt every `preStep`, cell size ≈ ship-diameter (≈ 32 px world units). Used by:

- `CollisionSystem` — bullet ↔ ship, bullet ↔ terrain tile.
- `BotAISystem` — nearest-enemy lookup.
- `WeaponFireSystem` (Persuadertron, Kicker AoE) — radius queries.

Rebuilt from a single thread per the engine contract; queries safe from workers.

### 4.4 Render path

2D rendering needs new infrastructure. **Out of scope for this plan to pick a backend** — that's milestone 1 work — but the constraints are:

- Existing `vulkan_renderer` static lib (`include/threadmaxx_vk/`) is set up for 3D `InstanceLayoutEntry` cubes. Either (a) extend it with a 2D pipeline (orthographic camera, sprite vertex layout, texture-array binding) or (b) start a parallel `threadmaxx_2d` static lib.
- Whichever path, the engine-side API stays the same: each render-relevant system writes to its `RenderFrameBuilder`, the engine merges into the back `RenderFrame`, the renderer reads on `submitFrame`.
- Particles and terrain tiles are the throughput pressure points. Both go through the auto-instanced `instances` lane (mass-spawned, `RenderTag + !DisabledTag`). Ships, HUD, debug overlay go through the hierarchical lane via `RenderFrameBuilder::addOpaque` etc.

### 4.5 Determinism

Fixed-step + commit hash + scripted tuning gives us replay for free. Inputs are captured per-tick into a flat append-only buffer; `Engine::commitHash` round-trips across replays. The Scripted tuning trace from `Tuning.hpp` rides shotgun on the input recording — same patch stream on playback, same scheduling, bit-identical state.

---

## 5. Compatibility tiers with original TOU

Ordered cheapest-to-deliver first. **The hard line: nothing from the original `TOU.exe` is reverse-engineered as code.** All compatibility flows through the on-disk asset formats, which are public via `toudoc_makelev.htm`.

### Tier 0 — Asset-format pass-through (milestone 1)
Drop-in support for the open formats the original already used:
- 24-bit uncompressed `.tga` attribute maps. Importer uses `stb_image` to load; pixel color → tile attribute per the `colors.png` legend.
- `.jpg` visual layers, parallax backgrounds, GG-theme textures. Importer loads via `stb_image`, uploads to GPU as an atlas slice.
- `.wav` / `.ogg` audio files. Backend plays directly.
- `info.txt`, `Normal.txt`-style `/KEY value` configs. Hand-rolled parser (≈ 100 lines).
- GG theme directories with their tagged naming (`s1.tga`, `t1.jpg`, `l1.tga`, etc.). Importer walks the directory, looks for tag patterns, builds an in-memory theme record.

**Acceptance**: drop `TOU/ggstuff/the earth/` into `examples/tou2d/assets/themes/the_earth/`, and the game renders levels generated from that theme.

### Tier 1 — Binary `.lev` import (milestone 2)
A standalone CLI `tou2d_import_lev <path/to/foo.lev> <output_dir>`:
1. Read the `TOU level file v1.4\r\n\x1a` magic. Reject otherwise.
2. Parse the binary header for width, height, theme name, author, embedded options.
3. Locate the embedded JPEG (`0xffd8` SOI), copy to `<output_dir>/foo.jpg`.
4. Locate the embedded TGA, copy to `<output_dir>/foo.tga`.
5. Emit `<output_dir>/foo.txt` in the `Normal.txt` schema with whatever options we successfully extracted.

The output is a Tier 0 asset bundle, so the game loads it identically to a fresh source build. Failure modes: fields we can't decode get sensible defaults; the level still loads.

**Acceptance**: the 4 shipped `.lev`s (`desert / jungle / minibase / woods`) round-trip cleanly. Manual playtest confirms attribute map matches the original.

### Tier 2 — Binary `.SHP` import (milestone 4)
A separate CLI `tou2d_import_shp <path/to/foo.SHP> <output_dir>` that produces:
- A PNG sprite atlas (one frame per rotation step, palettized via `data/Pal.col`).
- A small `.txt` ship-config (display name, stats blob fields once decoded).

Stats fields we can't decode default to "Basic ship" values; the user can override in the `.txt`.

**Acceptance**: the 9 shipped `.SHP`s import and are playable, even if some stats default. Pixel-perfect sprite rendering is a stretch goal.

#### M4.7 — Palette load + body visualizer (landed)

`examples/tou2d/PalCol.hpp` + `examples/tou2d/TgaWriter.hpp` are
header-only and shared between the CLI and `tests/tou2d_pal_col_test.cpp`.
The importer now accepts:
```
tou2d_import_shp <in.SHP> <outdir> [--palette <pal>] [--width N]
```
With `--palette` it writes `palette.tga` (128x128 swatch) and
`body_strip.tga` (a `--width`-wide visualization of body.bin
interpreted as raw 8-bit palette indices; default width 24). This is
a **best-effort speculative decode** — the actual sprite framing
inside body.bin is still opaque. The strip TGA gives a visual surface
for the user to eyeball whether 24 is the right hypothesis, or whether
a different width (26 / 28 / 32) lines frames up vertically.

**Empirical body-size evidence (collected from all 9 SHPs):**
Several ships share *identical* body sizes despite different total
file sizes — strongly suggesting a fixed per-rotation-step layout
that varies only with frame dimension class:

| Body size  | Ships                | Hypothesis            |
|------------|----------------------|-----------------------|
| 65483 B    | PERH, TIEF           | ≈ 24×24 frame class   |
| 75851 B    | BATM, BEE2           | ≈ 28×28 frame class   |
| 98891 B    | FLYY, XWIN           | ≈ 32×32 frame class   |
| 87, 55, 47 | DEST, PERU, SPED     | one-off (each unique) |

If frames are stored as raw indexed pixels at a fixed (W, H) per
ship and there are N rotation steps × M animation frames per step,
then `body_bytes = W × H × N × M + small_per_ship_header`. The next
batch (M4.7b) is: derive `(W, H, N, M)` by trying width/height pairs
that cleanly divide the body sizes above, eyeballing the resulting
strips for ship-shaped blobs, then promote the speculative decoder
to a real one.

#### M4.7b — Extended-header decode (landed, partial)

Header parser now extracts **frame dimensions, rotation count, and
max-HP** directly from the bytes around the per-ship anchor pattern
`WW 00 HH 00 18 20` (frame-width LE-u16, frame-height LE-u16, byte
0x18 = 24 rotation steps, trailing invariant 0x20). The anchor sits
at a per-ship-variable offset (`anchorOffset` field in `ParsedHeader`)
because the marker region between the stat-extra block and the
anchor differs in length per ship — search-based location is needed.

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

Notes:
- **Display names in the actual SHP files don't match the manual's
  stock-ship table.** The manual lists "Basic ship", "Bee", "Tie
  Fighter", etc.; the files say "Butterfly", "B2 Stealth fighter",
  "Imperium Tie Fighter". The `ManualEntry` table in the CLI is
  therefore unreliable for stem → display-name mapping; the parser's
  `displayName` field is authoritative.
- **W == H in all 9 files**, and **rotation count = 24 (0x18) in all
  9 files**. Plausibly invariants of the engine's sprite layout.
- **`statExtra[3]` varies (0x14 / 0x28 / 0x32)** — earlier hypothesis
  that it was always 0x32 was wrong. The value tracks weakness:
  Butterfly (PERH) is a 20-HP glass cannon; Speedie (SPED) is a
  40-HP fast frame; everything else maxes at 50.
- **First three bytes of `statExtra` remain unresolved** — they don't
  obviously correlate with the manual's Strength/Thrusters/Turning
  numbers. Likely engine-internal physics coefficients (mass / drag /
  turn-acceleration). Cross-checking with the playable demo could
  isolate which is which, but it's out of scope for the importer.

Body format remains opaque. The earlier hypothesis that body.bin was
raw indexed pixels at some width was **falsified**: none of the 6
distinct body sizes (47022, 55854, 65454, 75822, 86958, 98862) divide
cleanly by `W * H * N` for any plausible (W, H, N) — at 24 rotations
× ≥ 4 animation frames × W×H pixels we'd see 24 × 4 × 26² = 64,896
bytes for PERH's body of 65,454 bytes, which is close but not exact;
the gap grows for the other ships. Hex inspection shows the body is
dominated by `XX XX YY` triplet patterns with long zero stretches —
clearly a custom sparse/RLE encoding, not raw pixels. The strip-TGA
visualizer the previous batch shipped is still useful for spotting
pixel structure but should not be misread as a real decoder.

What's still unknown (next batch's work):
- **Body encoding rules**. The `XX XX YY` triplets dominate but the
  framing is unclear — per-row? per-frame? Is there a start-of-frame
  marker? An offset table? The ~200-byte mostly-zero stretch between
  the W/H/rot anchor and the first non-zero data could be an empty
  per-frame TOC reserved but never populated — or padding.
- **Per-ship marker region** (the bytes between `statExtra` and the
  W/H/rot anchor). 8 ships start with `?? 01 02 01 05 04`; XWIN's
  variant is `01 01 02 01 02 03`. Role unknown — possibly a section
  marker, possibly per-ship animation timing data, possibly version.
- **First three bytes of `statExtra`** — likely physics, but mapping
  to manual entries (mass / thruster force / turning rate) needs
  cross-checking against in-game behavior.

A future batch tackling the body decoder should treat it as serious
reverse-engineering work — pattern matching alone is insufficient,
and there's no public source / spec / oracle. Suggested approach:
write an interactive hex visualizer (probably in Python) that lets
the operator step through hypothesis decodings frame-by-frame, then
back-port the verified algorithm into a C++ decoder.

**Decode spike (committed: `examples/tou2d/scripts/decode_spike.py`)**

A stdlib-only Python tool that parses the same header bytes the C++
parser does and runs four decode hypotheses against the body:

| Variant | Rule                                                | PERH result        |
| ------- | --------------------------------------------------- | ------------------ |
| A       | row = `[skip][run][color]...` until 0x00 terminator | 0 opaque (failed)  |
| B       | row = `[N_runs][triplet * N_runs]`                  | 0 opaque (failed)  |
| C       | classic RLE: `[count][color]`, row wraps at width   | 0 opaque (failed)  |
| D       | pure `[skip][run][color]` triplet stream, no delim  | **584 opaque /676 in frame 0** |

A–C all fail the same way: the body's leading 269 zero bytes get
consumed as fake-empty frames (each `0x00` is interpreted as
some kind of terminator), so the decoder never reaches real pixel
data.

**Variant D is the breakthrough.** Treating the body as a flat
sequence of `[skip][run][color]` triplets with no row/frame
terminators means the leading zeros decode as `[skip=0][run=0]
[color=0]` no-ops (89 of them, 267 bytes), then bytes 270-272
`01 01 2d` are the first real triplet (skip 1, run 1, color 0x2d).
This is consistent with body size: PERH = 65,454 bytes = 21,818
triplets EXACTLY (no remainder). The other 8 ships likewise have
body sizes cleanly divisible by 3.

After exhausting all 21,818 triplets, the linear pixel cursor sits
at 592,705 pixels: 340,764 from `run` writes + 251,941 from `skip`
advances. **This does NOT divide cleanly** by 676 (= 26×26) or by
676 × 24 — so the encoding isn't a pure flat raster of fixed-size
frames. Frame 0 of the naive 24-frames-back-to-back slicing shows
clear pixel structure (86% opaque coverage, ship-density) in the
top half but bleeds solid-color into the bottom — meaning a frame
boundary marker exists that resets the cursor mid-stream, and the
naive slice mistakes that boundary for "more frame 0 data".

**Variant D was nearly right — the actual encoding turned out to be
even simpler.** The breakthrough came from combining the user's
parallel RE work (a 32-frame slicing-from-end-of-file hypothesis
documented in their untracked `tou_shp_mirror_pairs.py`) with a
cross-ship file-size audit. The Variant-D triplet bytes ARE the
sprite data; they're just not [skip][run][color]. See M4.7c below.

#### M4.7c — Body decoder LANDED ✅

**Layout (confirmed across all 9 stock SHPs):**

```
file_size = header_bytes + 32 * 3 * frame_w * frame_h
body_start = file_size - 32 * 3 * frame_w * frame_h
```

Per-ship header_bytes = ~592 + name_length:

| File | Size | Frame | 32×3×W×H | Header | Name length |
|------|------|-------|----------|--------|-------------|
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

Empirical channel role (visually verified — `scripts/decode_sprite.py`
emits a 32-rotation montage per ship; TIE fighters, X-wings, the Fly,
the Batman ship and Destroyer are all unmistakably recognizable):
- `b0` = hull palette index (primary silhouette)
- `b1` = edge / wing-highlight palette index
- `b2` = cockpit / center detail palette index

Recommended composite: **`b2` over `b0`** (cockpit overlays hull). The
`b1` channel currently isn't consumed by the renderer — it may be
animation-frame data (e.g. thruster on/off) or a self-shadow mask.
Palette index 0 is transparent. 32 rotations × 11.25° starts at
"ship facing down" and proceeds counter-clockwise (user-confirmed
hypothesis matches all 9 ships).

**The 0x18 = 24 byte at `anchor[4]`** in the header is NOT the body's
frame count — the body has 32 in every stock ship. Best guess: some
gameplay constant ("core" rotations for collision normals?). Not
consumed by the decoder.

**What's STILL not decoded** (now informational, not blocking):
- The ~500–550 bytes between the W/H anchor and the body start —
  per-rotation offset table? Animation timing? Weapon hardpoints?
  Renderer doesn't need them; body is fixed-stride.
- Per-ship marker region role (8 ships: `?? 01 02 01 05 04`; XWIN:
  `01 01 02 01 02 03`).
- `b1` channel purpose (sprite is clean with `b0+b2` composite).
- First three bytes of `statExtra` (physics coefficients — cross-check
  against in-game behavior).

**Code landed:**
- `examples/tou2d/ShpBody.hpp` — header-only `ParsedBody` + `parseBody`
  + `compositeRotation` (writes RGBA8 with `b2` over `b0`).
- `examples/tou2d/scripts/decode_sprite.py` — the definitive decoder /
  visualizer; emits per-ship 32-rotation montage PNGs.
- `tests/tou2d_shp_body_test.cpp` — 6 pinning cases (happy path,
  frame indexing, pixel triplet, composite priority, too-small,
  zero-dims, header-body overlap).
- `examples/tou2d/tou2d_import_shp.cpp` — when `--palette` is given,
  also emits `rotations.tga` (frame_w × 32 wide, frame_h tall composite
  sheet).

**Updated** `ShpHeader.hpp` docs: body layout deferred → landed.

#### M4.7d — Centering + color model (LANDED ✅)

The M4.7c composite worked but the sprites were visually off-center
in 31 of 32 frames, and the recommended `b2`-over-`b0` palette
composite produced rainbow accents where the original game shows
smooth team / cockpit colors. M4.7d closes both gaps.

**Toroidal-shift centering.** Body bytes are stored with the visible
ship wrapped around the (0, 0) origin of the frame; each frame (0..30)
embeds a single sentinel pixel at a deterministic position that marks
the wrap origin:

```
flat_position(N) = W * H - 6 * (31 - N)
```

The `+6` step matches a fixed 3-pixel metadata trailer that every
frame carries at the sentinel position:

| Offset | (b0, b1, b2) | Meaning |
|--------|--------------|---------|
| +0 | (0, 0, 2) | magic marker |
| +2 | (0, 24, 0) | rotation count (0x18, mirrors header `anchor[4]`) |
| +4 | (W, 0, W) | frame width |

For frame 31 the formula evaluates to `W*H` (one past the last pixel),
which is the encoder saying "no sentinel — this frame is already
centered, render straight through". Verified across all 9 stock ships.

**Centering recipe** (rotations 0..30):

1. Decode `(sx, sy)` from the sentinel formula.
2. Primary toroidal shift `(sx, sy + 1)`: `post(x, y) = pre((x + sx) % W, (y + sy + 1) % H)`.
   Using `sy + 1` puts the sentinel row at the BOTTOM (row H-1) rather
   than the top.
3. Secondary horizontal shift: top half (post-shift rows
   `0 .. H - sy - 2`) gets `+6` columns. Empirically dialled across
   TIEF/FLYY/XWIN/BATM/DEST/PERH/BEE2/PERU/SPED.
4. Trailer pixels at post-shift `(0, H-1)`, `(2, H-1)`, `(4, H-1)`
   cleared to transparent.

The sentinel-row strip lands "above" the ship in some rotations and
"below" in others — left as a TBD with a doc-comment note; visual
inspection at engine-side render scale will tell us whether to keep,
flip, or hide that strip.

**Color model.** The three bytes per pixel are NOT three palette
indices into the VGA palette. They are three independent **intensity
channels (0..255)**, one per shaded region:

- `b0` = hull intensity (cross-brackets, structural elements)
- `b1` = team intensity (wings / faction-color region — different team
  = different color, the original game's per-team recolor mechanism)
- `b2` = cockpit intensity (cockpit-sphere / center detail)

Final pixel = additive blend of `hull_color * b0/255 + team_color * b1/255
+ cockpit_color * b2/255` (per-channel saturating to 255), with alpha = 1
when any channel is non-zero. Verified by dumping TIEF frame 31: `b1`
has a smooth 0..255 gradient in two symmetric clusters (the wings),
`b2` has a smooth 0..255 gradient in the central cockpit blob, `b0`
has small values along the structural T-bar — exactly the three
overlaid shaded regions the original game ships display.

**Code landed:**
- `examples/tou2d/ShpBody.hpp` — `primarySentinel(W, H, N) -> optional<Sentinel>`,
  `ShipColors`, and `compositeRotationCentered` implementing the
  full recipe in a single pass. Legacy `compositeRotation` kept for
  back-compat with palette-index inspection tooling.
- `tests/tou2d_shp_body_test.cpp` — 11 cases total (5 new in M4.7d
  covering sentinel formula across PERH/FLYY/SPED, frame-31 no-shift
  special case, trailer suppression, blend-color math, and the
  primary+secondary shift composition).
- `examples/tou2d/tou2d_import_shp.cpp` — `rotations.tga` now uses
  `compositeRotationCentered` + the built-in `ShipColors` defaults.

**Still TBD** (deferred to M4.8 alongside renderer wiring):
- Per-team `ShipColors` table (each NPC / player needs a faction
  color; the current `ShipColors{}` defaults are a single neutral
  palette for the importer's preview).
- The sentinel-row strip's correct disposition (above/below/hidden).

#### M4.8 — Runtime sprite rendering + audio (LANDED ✅)

**Sprite path — chose the "foreground overlay" design instead of a
new sprite_quad pipeline.** The plan called for a brand-new Vulkan
pipeline with per-instance UV rects; in practice the existing
background pipeline IS already a textured world-space quad and a much
smaller change delivers visible sprites just as well:

1. **Alpha blending added to `buildBackgroundPipeline`** (`SRC_ALPHA`,
   `ONE_MINUS_SRC_ALPHA`). No effect on background draws because the
   JPG is alpha-padded to 255 by the host; lets the pipeline be reused
   for transparent overlays.
2. **`setForegroundFromRgba` / `updateForegroundRegion` /
   `setForegroundWorldExtent`** on `VulkanRenderer` mirror the
   background's API. Same pipeline, separate descriptor set, separate
   texture handle, separate `worldRect` push constant. The foreground
   quad is drawn ONCE in `recordFrame` after every camera pass and
   before `vkCmdEndRendering`, so sprite pixels composite over opaque
   cubes + debug overlays cleanly.
3. **`SpriteAtlas`** (header-only, `examples/tou2d/SpriteAtlas.hpp`) —
   reads a TOU `.SHP` from disk, decodes the header + body, composites
   all 32 rotations through `compositeRotationCentered` with a
   caller-supplied `ShipColors`. Returns 32 RGBA frames at the SHP's
   native `frameWidth × frameHeight`.
4. **`ShipSpriteRef`** user component — single `atlasIdx` index into
   the compositor's atlas table. Per-tick blit cache lives in the
   compositor itself (`unordered_map<uint32_t, PrevBlit>` keyed by
   `EntityHandle::index`), keeping the user-component a trivially
   copyable 8-byte POD safe to memcpy across mask changes.
5. **`SpriteCompositor`** (header-only,
   `examples/tou2d/SpriteCompositor.hpp`) — owns the foreground RGBA
   buffer sized to the level extent. Each tick (`compositor.tick(world,
   ids)` from `main.cpp` after `engine.step()`):
   * Walks every chunk with `Transform + ShipSpriteRef + !DisabledTag`
     via `World::forEachChunkOf`.
   * Recovers heading from `Transform.orientation` via the same
     `atan2`-on-pure-Z-quat formula `MovementSystem` uses.
   * Maps heading to frame index — SHP frame 0 = ship facing down,
     `θ = π` in the engine's "+Y is forward" convention. Frame index =
     `round((θ - π) / (2π/32)) mod 32` (32 CCW steps from "down").
   * Clears the previous blit's bbox, blits the new rotation centered
     on the projected screen pixel, unions both bboxes into a dirty
     rect.
   * `main.cpp` consumes the dirty rect and calls
     `renderer->updateForegroundRegion(...)` once per frame.
6. **Per-team `ShipColors`** — `kSlotColors[4]` in `SpriteAtlas.hpp`:
   slot 0 yellow, slot 1 blue, slot 2 red, slot 3 green. Only the team
   intensity channel maps to a per-slot color; hull stays neutral
   grey, cockpit stays warm amber — matches the original game's "one
   color identifies your faction" convention.
7. **Cube-render fallback preserved.** Ships whose `.SHP` failed to
   load (or for which no SHP was provided) keep `RenderTag` and render
   via the existing cube-instance lane. Ships that successfully loaded
   an atlas spawn WITHOUT `RenderTag` so no cube draws under the
   sprite.

**Audio plumbing.** `AudioSystem` (`AudioSystem.hpp/.cpp`) wraps
miniaudio (system header at `/usr/include/miniaudio/miniaudio.h`,
`#define MINIAUDIO_IMPLEMENTATION` in exactly the system's TU). On
construction it inits an `ma_engine` device, pre-decodes a small bank
of WAVs into `ma_sound` instances, and subscribes to the typed
`AudioPlay` event channel. Event handler resets the sound's PCM
cursor + starts it — polyphony is one-instance-per-sound (rapid
re-trigger cuts off the prior shot, which feels right for the
200ms-laser-zap SFX shape).

Sound bank (5 entries):
- `audio::kSoundDumbfire`  → `fire_r.wav` (basic weapon)
- `audio::kSoundSpread`    → `PHOT_R.WAV` (spread / photon)
- `audio::kSoundHit`       → `hit1_m.wav` (bullet hits ship)
- `audio::kSoundExplode`   → `exp1_m.wav` (ship death)
- `audio::kSoundTileBreak` → `BRIC_R.WAV` (terrain destruction)

Emit sites (all guarded by a borrowed `engine_` pointer — pass
nullptr to suppress audio cues):
- `WeaponFireSystem` — emits Dumbfire/Spread on each successful fire.
- `BulletShipCollisionSystem` — emits Hit on damage, Explode on the
  alive→dead transition.
- `BulletTerrainSystem` — emits TileBreak when a tile's HP rolls to 0.

**Asset staging.** `assets/sfx/` holds the 5 WAVs; `assets/ships/`
holds the 4 SHPs the game spawns (TIEF, BEE2, XWIN, DEST — one per
slot). All sourced from the user's `TOU/` install. The `assets/`
directory is **gitignored** — the original game's binary assets are
not redistributable, so each user copies their own files. The runtime
degrades gracefully when files are missing (sprite ships fall back to
cube rendering; audio cues become silent no-ops). Concretely:

```
assets/
├── ships/
│   ├── TIEF.SHP  # P1
│   ├── BEE2.SHP  # P2
│   ├── XWIN.SHP  # P3
│   └── DEST.SHP  # P4
└── sfx/
    ├── fire_r.wav   # dumbfire
    ├── PHOT_R.WAV   # spread
    ├── hit1_m.wav   # bullet hit
    ├── exp1_m.wav   # ship explosion
    └── BRIC_R.WAV   # terrain destruction
```

`TOU2D_ASSETS=<path>` env var overrides the default `./assets`.

Audio init is silent on success; missing files log to stderr and that
sound becomes a no-op without affecting the rest of the bank.

**Code landed:**
- `examples/vulkan_renderer/src/VulkanPipelines.cpp` — alpha blending
  on the background pipeline.
- `examples/vulkan_renderer/include/threadmaxx_vk/VulkanRenderer.hpp`
  + `examples/vulkan_renderer/src/VulkanRenderer.cpp` —
  foreground-texture descriptor + 3 new public methods +
  end-of-frame foreground draw.
- `examples/tou2d/SpriteAtlas.hpp`,
  `examples/tou2d/SpriteCompositor.hpp` — new.
- `examples/tou2d/AudioSystem.hpp`, `examples/tou2d/AudioSystem.cpp` —
  new; miniaudio impl unit.
- `examples/tou2d/DemoTypes.hpp` — `ShipSpriteRef`, `AudioPlay`,
  `audio::*` IDs, new user-component slot.
- `examples/tou2d/TouGame.{hpp,cpp}` — register sprite UC, load
  atlases, register `AudioSystem`, pass engine pointer to weapon /
  bullet-vs-terrain systems for AudioPlay emits.
- `examples/tou2d/main.cpp` — construct compositor, install the
  foreground texture, drive `compositor.tick` + dirty-rect upload
  each frame.
- `examples/tou2d/WeaponFireSystem.{hpp,cpp}`,
  `examples/tou2d/BulletShipCollisionSystem.cpp`,
  `examples/tou2d/BulletTerrainSystem.{hpp,cpp}` — emit AudioPlay.
- `examples/tou2d/CMakeLists.txt` — `AudioSystem.cpp` source +
  `Threads::Threads` / `dl` / `m` link deps (miniaudio's Linux
  requirements).
- `assets/sfx/{fire_r,PHOT_R,hit1_m,exp1_m,BRIC_R}.{wav,WAV}` and
  `assets/ships/{TIEF,BEE2,XWIN,DEST}.SHP` — staged from `TOU/`.

**Smoke verification.** `threadmaxx_tou2d 300 --level <jungle>` runs
to completion with `[tou2d] foreground sprite layer installed=1` and
no audio errors logged.

**Still TBD** (deferred follow-ups, NOT blockers for M4 acceptance):
- Audio device init is silent — a non-headless run is the proof that
  sounds actually reach the speakers.
- Sentinel-row strip disposition (above/below/hidden — flagged in
  M4.7d) is whatever the importer's compositing leaves; visual
  inspection at engine scale may want refinement.
- Music (`assets/music/level*.ogg`) — files staged but no driver
  wired; the AudioSystem only carries SFX today.
- BotControlSystem could emit AudioPlay too (currently no bot-cue
  emits) — left out to keep this batch focused.

### Tier 3 — Native source-asset workflow (milestone 5+)
Skip the binary containers entirely. The user's editing workflow becomes:
- Paint `<level>.jpg` (visual) and `<level>.tga` (attribute map) in Photoshop/GIMP exactly per the original's `colors.png` legend.
- Write `<level>.txt` per `Normal.txt` schema.
- Drop all three into `assets/levels/<level>/`. Engine picks them up.

This is the workflow the original's manual already documents. We just don't require the `makelev.exe` compile step.

**Acceptance**: a TOU level designer who already has their source `.jpg + .tga + .txt` working bundle from circa-2002 can copy it into the new project's `assets/levels/` and play it without touching binaries. No `makelev.exe` needed.

### Explicit non-goals (compatibility-wise)
- `options.cfg` — opaque, replace with our own JSON/TOML player profile.
- `.gfx` / `.col` / `.dat` / `.tau` in `data/` — proprietary containers we don't decode.
- `fmod.dll` / `ijl10.dll` — licensed third-party; we replace with our own backends.
- Save-game compatibility — there are no save games in the original; nothing to migrate.

---

## 6. Project layout

```
examples/tou2d/
├── CMakeLists.txt                — wires the binary + opt-in CLI tools
├── TOU_PLAN.md                   — this document
├── main.cpp                      — Game entry point
├── TouGame.{hpp,cpp}             — IGame implementation
├── DemoTypes.hpp                 — UserComponent POD definitions
├── input/                        — InputSystem + key-binding tables
├── movement/                     — Movement + Gravity + Collision systems
├── weapons/                      — One source pair per weapon family
├── terrain/                      — TerrainSystem + tile-grid encoding
├── render/                       — Camera, Hud, ParticleRenderer, sprite atlas mgmt
├── ai/                           — BotAISystem
├── audio/                        — AudioTriggerSystem + miniaudio glue
├── import/                       — Tier 1 / Tier 2 binary importers
│   ├── tou2d_import_lev.cpp
│   └── tou2d_import_shp.cpp
├── assets/
│   ├── themes/                   — Tier 0: drop GG themes here
│   ├── levels/                   — Tier 3: drop source `.jpg + .tga + .txt` bundles
│   ├── ships/                    — Imported / hand-drawn ship sprite atlases
│   ├── sounds/                   — Drop original `.wav`s
│   └── music/                    — Drop original `.ogg`s
└── tests/
    ├── tou2d_lev_import_test.cpp — Tier 1 round-trip
    ├── tou2d_theme_load_test.cpp — Tier 0 GG theme parse
    └── tou2d_replay_test.cpp     — input recording + deterministic playback
```

### CMake gating
`THREADMAXX_BUILD_EXAMPLES=ON` (the project default) registers `tou2d`. Like `vulkan_renderer` and `rpg_demo`, the binary is **skipped silently when the 2D render backend isn't present** — pick that backend in milestone 1 before turning it on.

The two importer CLIs (`tou2d_import_lev`, `tou2d_import_shp`) build unconditionally as long as `tou2d` builds — they don't need the renderer.

### License posture
- The new project's source: same license as threadmaxx (no derivative work from `TOU.exe`).
- The `TOU/` install directory in this repo: **do not redistribute**. Add `TOU/` to `.gitignore` if it's currently tracked; it's an inspection-only artifact on the local box. (Check: currently visible in `ls` but not staged per git status snapshot in CLAUDE context.)
- Anything we generate from a user-owned TOU install (Tier 1 / Tier 2 outputs) is the user's responsibility — same posture as Doom WAD importers in modern source ports.

---

## 7. Milestone breakdown

Mirrors the ChatGPT notes' suggested milestone order, refined with TOU-import work folded in.

### Milestone 1 — Engine + 2D renderer foundation (largest single piece)
- Pick the 2D render backend (extend `threadmaxx_vk` ortho path OR new `threadmaxx_2d`).
- Sprite atlas resource type, registered through `ResourceRegistry`.
- `Camera` user-component-equivalent (engine `Camera` is already a render-contract POD; we re-use it directly with `ProjectionMode::Orthographic`).
- Empty world, ship POD, MovementSystem, GravitySystem. Single player, no weapons, no terrain. Just "thrust around an empty screen."
- **Acceptance**: a single ship visibly accelerates with arrow keys; gravity pulls it down; closes cleanly.

### Milestone 2 — Terrain + destructible tiles + Tier 0/1 import
- Tile-grid `TerrainBlock` with attribute byte + HP.
- Visual layer: rendered as a single screen-sized quad with the level's JPG texture.
- Collision pass against attribute-map cells (per-pixel-attribute → per-tile-attribute lookup at load).
- `tou2d_import_lev` CLI (Tier 1) producing `assets/levels/<name>/{visual.jpg, attribute.tga, config.txt}`.
- Loader reads the `.txt` config + `.tga` + `.jpg` and populates `TerrainBlock` entities at startup.
- **Acceptance**: `tou2d_import_lev jungle.lev` produces a directory; loading that directory shows the original Jungle level's visuals + the ship collides with the right shapes.

### Milestone 3 — Weapons + projectiles + damage + multiplayer
- `WeaponFireSystem` + the 10 v1 weapons.
- `ProjectileSystem`, `Particle` POD, `CollisionSystem`.
- `TerrainDamageSystem` mutates `TerrainBlock` HP / attribute; when HP hits 0, tile becomes `Attribute::Air`.
- 2-4 local players via input device routing (P1 keyboard, P2-P4 same keyboard with the original's bindings — same-keyboard 4-player works per `toudoc_controls.htm`).
- Respawn after death, kill scoring.
- **Acceptance**: 2 humans can play a deathmatch on jungle; terrain visibly deforms; round ends on score limit.

### Milestone 4 — Bots + Tier 2 import + content expansion (LANDED ✅)
- `BotAISystem` (~150 lines of state-machine: maintain distance / aim / retreat / fire). — landed M3.4/M4.1.
- Fill empty slots with bots up to 4 ships total. — landed M3.3.
- `tou2d_import_shp` CLI (Tier 2). The 9 original ships become drop-in usable. — landed M4.6/M4.7c/M4.7d (sprite decoder + per-team color tinting).
- Match modes: deathmatch, last-ship-standing. — landed M4.3.
- HUD: score per player, weapon icon, HP bar. — landed M4.4.
- Sound: AudioTriggerSystem with miniaudio backend; drop `sfx/` and `music/` into `assets/`. — landed M4.8 (SFX bank wired; music staged but driver follows in M5).
- **Acceptance**: 1 human + 3 bots deathmatch on jungle with original sounds and original ship sprites runs at fixed 60 Hz for ≥ 5 minutes with no crashes / OOMs. — bounded `300 ticks` smoke run lands the sprite layer + audio bank with no errors; full 5-minute interactive run is for the user's machine to verify.

### Milestone 5 — Polish + replay (stretch) — **COMPLETE ✅**
- M5.1 — configurable humans/bots + split-screen cameras (**LANDED ✅**).
- Shared dynamic camera framing all live ships (now superseded by M5.1's
  per-human split — bot ships do not get cameras, see § M5.1 below).
- Particle fidelity polish (explosions, debris, smoke) — M5.3 ✅.
- Replay capture (input log + commit-hash stream) + playback — M5.4 ✅.
- Procedural-level generation pass (Tier 0 GG themes already imported, just need the generator) — M5.5 ✅.
- More weapons toward the original's 50+ — M5.6 + M5.7 + M5.8 ✅ (10 specials + Dumbfire = 11 total).
- **Acceptance**: replay round-trips bit-identical (`commitHash` stream matches frame-for-frame) — verified across every special-weapon variant + the procedural generator + repair pickups (M5.4 onwards).

#### M5.1 — Configurable humans/bots + split-screen (LANDED ✅)

**Player counts.** `--humans=N` (1..4) and `--bots=M` (0..63) flags
select the match composition. Defaults `1 + 3` so prior smoke tests
keep their shape. The two flags compose freely with `--mode=` and
`--level <path>`; a combined minimum of `humans + bots >= 2` is
enforced at parse time so a match always has at least two ships.
Hard ceilings come from `examples/tou2d/DemoTypes.hpp`:

```cpp
inline constexpr std::uint8_t kMaxHumans = 4;          // 4 keyboard rows
inline constexpr std::uint8_t kMaxBots   = 63;         // matches original TOU
inline constexpr std::size_t  kMaxPlayerSlots =        // = 67
    static_cast<std::size_t>(kMaxHumans) +
    static_cast<std::size_t>(kMaxBots);
```

Per-slot bookkeeping arrays in `BulletShipCollisionSystem` (dmg /
firstShooter / killerByVictim / killsBySlotPostTick / ships gather),
`BulletTerrainSystem` (tilesDelta), and `BotControlSystem`
(rngBySlot / retreating / wanderTicksLeft / wanderAngle /
aimWobblePhase) all size to `kMaxPlayerSlots`. `LocalPlayer::slot` is
`uint8_t` so the hard ceiling stays at 255 ship slots even if the
constants grow.

**Spawn layout.** Slot 0 stays at the world origin (P1) so the
existing headless smoke test still finds it. Slots 1..N are placed on
a ring of radius `max(40, 18 * sqrt(N))` world units — empirical pick
that keeps spacing roughly constant as N grows from 4 to 67. Sprite
atlases are loaded once for the 4 canonical palettes (yellow / blue /
red / green); bots beyond slot 3 cycle their atlas via `slot % 4` so
team colors repeat in the expected order without per-bot atlas
duplication. Ship kinds (Basic / Bee / X Wing / Destroyer) cycle the
same way.

**Split-screen camera layout.** `CameraSystem` is now multi-camera:
it latches every HUMAN ship's position (`isBot == 0`) and emits one
camera per human via `RenderFrameBuilder::addCamera`, each carrying a
normalized `Camera::viewport` rect:

```
numHumans = 1 → { 0.0, 0.0, 1.0, 1.0 }                 full screen
numHumans = 2 → slot 0 { 0.0, 0.0, 0.5, 1.0 } (left)
                slot 1 { 0.5, 0.0, 0.5, 1.0 } (right)
numHumans = 3 → slot 0 { 0.0, 0.0, 0.5, 0.5 } TL
                slot 1 { 0.5, 0.0, 0.5, 0.5 } TR
                slot 2 { 0.0, 0.5, 0.5, 0.5 } BL
                — quadrant BR has no camera; the framebuffer's
                  default clear color (renderpass-time) leaves it
                  black, satisfying the user's "one section is black"
                  requirement without any special-case rendering.
numHumans = 4 → 2×2 grid (slot 0 TL, 1 TR, 2 BL, 3 BR)
```

Bots **never** get a camera. The renderer already supported per-
camera viewports via `Camera::viewport` (§3.11.2 batch D2; landed
2026-05-20), so no renderer changes were required — the camera
system just sets the field. Each camera independently computes its
view/proj matrix using the per-viewport aspect ratio
(`viewportAspect()`), so a half-width or quarter-screen quadrant
still has square pixels.

**Per-viewport HUD.** `HudSystem` rewrote its anchor model: every
human's badge / score-pip row / HP bar / ammo strips pin to the
TOP-LEFT corner of THAT human's viewport in world units (relative to
their own camera follow center). The old "4 corners of a single
camera" layout is gone. Bots never see a HUD (the loop iterates only
slots `[0, numHumans)`). The winner banner anchors to slot 0's
follow center.

**Known limitation.** Debug geometry is world-space; if two humans
fight in close enough quarters that one camera's view rect overlaps
another's, the HUD elements drawn at each player's view-corner will
appear in both viewports. In typical play (players move apart to
shoot at each other) this never happens; in pile-on close combat,
each player's HUD pips can briefly leak into a neighbor's quadrant.
A future M5.x batch could plumb camera-id-scoped debug-geom through
the renderer; for v1 the leak is documented and accepted.

**Code landed:**
- `examples/tou2d/DemoTypes.hpp` — `kMaxHumans` / `kMaxBots` /
  `kMaxPlayerSlots` constants.
- `examples/tou2d/main.cpp` — `--humans=N` / `--bots=M` arg parsing +
  total-≥-2 validation + propagation via `game.setPlayerCounts`.
- `examples/tou2d/TouGame.{hpp,cpp}` — `setPlayerCounts(numHumans,
  numBots)`; `playerShips_` is now `std::vector<EntityHandle>`; spawn
  loop iterates `[0, numHumans+numBots)` placing ships on a radius-
  scaled ring; `numHumans_` propagated to `CameraSystem` via
  `setNumHumans`.
- `examples/tou2d/CameraSystem.{hpp,cpp}` — per-human follow array;
  `viewportFor(humanSlot)` returns the normalized rect for the
  current layout; `buildRenderFrame` emits one camera per human;
  helper accessors `numHumans()`, `viewportAspect()`,
  `followCenter(slot)`.
- `examples/tou2d/HudSystem.cpp` — replaced 4-corner single-camera
  HUD with per-viewport top-left HUD; winner banner anchors to slot 0.
- `examples/tou2d/BulletShipCollisionSystem.cpp` /
  `BulletTerrainSystem.cpp` — per-slot arrays grew 16 → 67.
- `examples/tou2d/BotControlSystem.{hpp,cpp}` — per-slot RNG /
  retreat / wander / aim-wobble arrays grew 4 → 67; ctor seeds every
  slot deterministically.

**Smoke matrix.** Bounded runs at `--level <jungle>`:

| Args                          | Result |
|-------------------------------|--------|
| (defaults — 1h+3b)            | exits 60-tick smoke cleanly with `[tou2d] players: 1 human + 3 bots` |
| `--humans=2 --bots=2`         | exits 60-tick smoke cleanly |
| `--humans=3 --bots=0`         | exits 60-tick smoke cleanly |
| `--humans=4 --bots=10`        | exits 60-tick smoke cleanly |
| `--humans=1 --bots=63`        | exits 120-tick smoke cleanly (67 ships total) |
| `--humans=0`                  | rejects: `expected 1..4` |
| `--humans=1 --bots=0`         | rejects: `need at least 2 entities total` |
| `--bots=64`                   | rejects: `expected 0..63` |

`ctest -R tou2d` continues to pass all 3 tests (shp_import, pal_col,
shp_body). Clean build under `-DTHREADMAXX_WARNINGS_AS_ERRORS=ON`.

**Still TBD** (not blockers — explicitly out of M5.1 scope):
- The "shared dynamic camera that scales zoom to fit" mode from § 1
  is superseded by per-human split; if a "frame all live ships"
  spectator camera is wanted later, it lands as a new system reading
  every Transform.
- HUD leak in close combat (see "Known limitation" above).
- Interactive (non-headless) verification that split-screen quadrants
  composite correctly — bounded smoke confirms wiring but not pixel
  correctness; the user runs the unbounded binary on their machine to
  see the actual layout.

#### M5.2 — Polish round 1 (LANDED ✅, 2026-05-28)

Follow-up fixes after the user ran M5.1 on their box and reported a
batch of issues. Touches no engine internals — all edits land under
`examples/tou2d/` and `examples/vulkan_renderer/`.

**Split-screen background + foreground per camera (renderer):**
`VulkanRenderer::Impl::recordCamera` now issues both the world-
anchored background draw AND the foreground sprite-layer draw
INSIDE the camera loop, using each camera's own `viewProj`. The
previous code drew background once pre-loop and foreground once
post-loop using `cam[0]` — visually correct in single-camera mode
but obviously wrong in split-screen (non-primary viewports showed
no background at all and the foreground sprite layer was placed by
P1's viewProj across the whole framebuffer, so the other viewports
showed P1's sprite arrangement at the wrong world position).

**Random spawn for all ships (TouGame):** dropped the slot-0-at-origin
ring spawn; every ship now samples a random Air cell via
`sampleRandomRespawn`, falling back to the ring on grid failure.
Headless smoke test still finds P1 via `playerShip()` (handle, not
position).

**Bot AI calmer + terrain-aware (BotControlSystem):** removed the
per-tick xorshift chaos that previously perturbed the engaged-aim
heading by ±3.4° every tick (visible as jittery turn flickering
near the fire arc edge). Sine wobble period slowed from ~580 ms to
~1.3 s. Added a 3-ray terrain ray-caster (forward + ±32° feelers
@ 60 wu lookahead): on a forward hit the bot picks the side with
more open feeler space, latches the turn direction for 300 ms,
and stops thrusting if the forward hit is <45% of the lookahead.
Wired by `TouGame::onSetup` via `botControl->setTerrainGrid(&grid_)`.

**Reload rework (DemoTypes + WeaponFireSystem):** basic weapon is
now infinite-fire on a 0.5 s (30-tick) cooldown — no magazine, no
reload. Special (spread) has a 1-shot magazine and reloads 1.25 s
after each burst. `kDumbfireReloadTicks = 0`, magazine pinned at 1
for visual "ready" pip; WeaponFireSystem skips the dumbfire
ammo-decrement + reload-start branch.

**Death sprite + overlap fix (SpriteCompositor):** rebuilt
`tick()` as a three-pass routine — (A) clear EVERY cached prev
rect, (B) blit every visible ship and re-record its prev rect,
(C) drop cache entries we didn't see this tick. Solves two bugs:
dead ships now vanish the same tick they go into DisabledTag
(Pass A clears the rect; Pass C drops the cache entry), AND
overlapping sprites no longer produce a black-patch artifact when
their bounding rects cross (all prev clears happen before any
blit).

**Denser terrain + wider destruction (DemoTypes + main):**
`kImportedPxPerTile` dropped from 8 → 4 (4× denser destruction
grid). The `BackgroundPainter::paintTile` overpaints a +1-px
bleed past each cell's edge to cover seam residue between adjacent
wrecked cells (rev 2 narrowed from the original +2 — 2 px was too
aggressive on the denser 4 px grid and overwrote outlines of
neighbouring still-alive tiles). The legacy M3.4 edge-fade
(chebyshev-distance darken envelope + ±10% xor-hash jitter) is
preserved, so destroyed patches read as rocky scarring rather
than a flat black rectangle, while the +1-px overlap still kills
the inter-tile seam.

**Bullet visuals tightened (WeaponFireSystem):** `kMuzzleOffset`
18 → 10 wu (bullet spawns at the ship's nose tip, not 4 wu ahead
of it); `kBulletScale` 4 → 2.4 (smaller visual footprint, less
target obscuration on sustained fire).

**Ship stat rebalance (DemoTypes):** all 9 kinds rebased on a
(strength=2.5, thrust=2.5, turn=2.5) baseline with a single
25%-bonus stat (`kBalancedKindBonus = 1.25`) marking each kind's
primary character. Replaces the prior 6× spread (Bee 10× thrust,
Destroyer 6× strength) with subtle differentiation. Basic = pure
baseline.

**Smoke matrix passes** under both Release and `-Werror`:
1+3 (default), 2+2, 3+0, 1+63 all complete bounded ticks; full
`ctest` (131 tests) green; tou2d unit tests still pass.

#### M5.3 — Particle FX polish (LANDED ✅, 2026-05-28)

Layered on top of M5.2's just-cleaned death-sprite path. New file
`examples/tou2d/ParticleSystem.{hpp,cpp}` owns a fixed-size pool
(`kMaxParticles = 256`) of pure-POD `Particle` records and emits
world-space `DebugPoint` instances each frame via the existing
debug-geom path — no new renderer pipeline, no new public engine
surface. Three FX families, each tuned to a different motion model:

| Effect | Trigger | Count | Color | Lifetime | Gravity |
|---|---|---|---|---|---|
| Death debris | `ShipLifecycleSystem` alive→dead | 16 | slot RGB | 35-55 ticks | +220 wu/s² (falls) |
| Death smoke | same | 10 | dark gray | 60-90 ticks | -45 wu/s² (rises) + drag |
| Impact spark | `BulletShipCollisionSystem` on hit | 5 | dumbfire=yellow, spread=orange | 8-16 ticks | 0 |
| Tile-break dust | `BulletTerrainSystem` on cell HP=0 | 6 | dirt brown | 18-28 ticks | +220 wu/s² (falls) |

`ParticleSystem` registers as its own no-conflict wave (`reads = {}`,
`writes = {}`) placed FIRST in registration order. Its `update()`
integrates existing particles BEFORE any emitter runs, so freshly
spawned particles render at their spawn position this tick and start
integrating next tick — clean spawn frame, no half-tick drift.

`buildRenderFrame()` walks the pool and emits one `DebugPoint` per
active particle, with alpha = `(ttl/maxTtl)^exponent` (exponent per
kind: debris=1.4 "stays bright then crashes", smoke=0.85 "smooth
fade", spark=2.0 "bright flash") and `pixelSize` linearly interpolated
toward `kPxShrinkMin = 0.6` as ttl runs out.

Pool overwrite is round-robin via a `head_` index — past 256
simultaneous particles the oldest get clobbered first. At 26
particles per ship death + 5 per bullet hit + 6 per tile break, 256
gives ~10 deaths worth of headroom; in practice the pool clears out
inside ~90 ticks (worst-case smoke lifetime).

Determinism: `std::mt19937{0xFEEDBEEFu}` seed, fixed across runs.
Same emission stream → same render stream tick-for-tick. The 8-ray
debug-line starburst in `ShipLifecycleSystem::buildRenderFrame` is
PRESERVED — particles layer on top as the lingering aftermath; the
line burst still serves as the bright initial flash.

CMake gating: `ParticleSystem.cpp` added to `tou2d_core`'s source
list; no new dependencies. ShipLifecycleSystem /
BulletShipCollisionSystem / BulletTerrainSystem each gained a
`setParticleSystem(ParticleSystem*)` setter and a borrowed pointer
field — null is safe for host-side tests that don't wire it.

**Build matrix passes** under both Release and `-Werror`:
1+3 (default), 2+2, 1+63 all complete bounded ticks cleanly; full
`ctest` (131 tests) green.

#### M5.4 — Replay capture + playback (LANDED ✅, 2026-05-28)

New files `examples/tou2d/Replay.{hpp,cpp}` define a tiny on-disk
format (`.tou2drec`, magic `'TOUR'`, version 1, 32-byte header +
`levelDirLen` bytes + per-tick `[PlayerInput × 4][commitHash u64]`)
and two classes: `ReplayRecorder` (open + appendTick + close) and
`ReplayPlayer` (open + advance + inputs/commitHash accessors). 40
bytes per tick → 2.4 KB/s at 60 Hz; a 5-min match weighs ~720 KB.

**CLI surface** (added to `main.cpp`):

| Flag | Effect |
|---|---|
| `--record <path>` | After each step writes the four keyboard rows + `engine.stats().commitHash` to `<path>`. Header captures `--humans` / `--bots` / `--mode` / `--level` for self-describing playback. |
| `--play <path>` | OVERRIDES `--humans` / `--bots` / `--mode` / `--level` from the file's header, wires the player into `InputSystem`, and after each step compares `engine.stats().commitHash` against the recorded value. Exit code 3 on any mismatch (first 4 mismatches are logged); 0 otherwise. |
| `--record + --play` | Rejected at parse time (mutex). |

**Why all 4 keyboard rows are recorded, not just `numHumans`.** The
pre-M5.4 `InputSystem::preStep` calls `readKeys(window, slot)` for
every `LocalPlayer` entity, including bot ships with `slot < 4`.
For `--humans=1 --bots=3`, slot 1's bot transiently receives a
nonzero `PlayerInput` command (whatever WASD reads at that instant)
before `BotControlSystem` overwrites it — the bot's final state is
unaffected but the keyboard read DOES land in `commitHash`. Replay's
`inputs(slot)` mirrors the same shape: returns the recorded value
for slot 0..3, default-zero for slot ≥ 4 (matches keyboard mode's
out-of-range guard). The fixed 32-byte per-tick footprint kills the
edge case with zero behaviour change vs the pre-M5.4 system.

**Why bots aren't captured.** `BotControlSystem` is fully
deterministic: per-slot `mt19937` seeded from a constant in its
ctor, no wall-clock reads. Same match config + same input stream
→ same bot decisions. The per-slot RNG state is implicit (engine
init replays it identically on both runs).

**Determinism guarantee.** The replay format pins the engine's
FNV-1a basis (`0xcbf29ce484222325`) into the header — a future
engine knob that shifted commitHash would refuse to open old
recordings rather than silently mis-replaying them.

**Capture timing.** `glfwPollEvents()` snapshots the OS key state
into GLFW's internal buffer; subsequent `glfwGetKey` reads from
that buffer until the next `pollEvents`. The main loop's order is
`pollEvents → [sample → record] → step → [record commitHash]`, so
the sampled inputs are byte-identical to what `InputSystem` reads
inside the step. The free function `tou2d::readKeyboardSlot()` is
the SAME reader InputSystem uses internally; main.cpp imports it
from `InputSystem.hpp` to guarantee they can't drift.

**Round-trip smoke matrix** (record N ticks → play N ticks under
the same binary):

| Config | Ticks | Verified | Mismatches |
|---|---|---|---|
| 1h+3b DM (default) | 120 | 120 | 0 |
| 2h+2b LSS          | 200 | 200 | 0 |
| 1h+63b DM          |  60 |  60 | 0 |

Round-trip also confirmed under `-Werror` build (`1h+3b DM 120t`).
Full `ctest` (131 tests) green. Final ship position matches
recording vs playback bit-for-bit in all three cases.

**Acceptance**: M5's stated acceptance criterion — "replay
round-trips bit-identical (`commitHash` stream matches
frame-for-frame)" — lands here.

#### M5.5 — Procedural-level generator (LANDED ✅, 2026-05-28)

New file `examples/tou2d/ProceduralLevel.hpp` (header-only) +
`tests/tou2d_proc_gen_test.cpp` exercise the generator. Algorithm,
deterministic from a single `std::mt19937(cfg.seed)`:

1. `cellsX/Y` from `kGgLevelCells[ggLevel]` — 48 / 80 / 112 / 160 /
   208 for levels 0..4.
2. `grid.reset(...)`.
3. Bottom 1/8 of canvas filled solid (ground floor).
4. `nBlobs ≈ stuffDensity × area / 5000`, clamped to `[1, 256]`.
   For each blob: random center in the upper canvas, random
   `rX ∈ [3, 9]` / `rY ∈ [2, 6]`, elliptical solid stamp.
5. Optional 1-cell bedrock perimeter ring (`hp = 0xFF`).

**CLI surface** (added to `main.cpp`):

| Flag | Effect |
|---|---|
| `--gen` | Enable generator with default config (seed 0, ggLevel 2, density 50, perim on). |
| `--gen=<N>` / `--gen-seed=<N>` | Set `genCfg.seed`. Strtoul-parsed, so `0x…` works. |
| `--gen-level=<0..4>` | Sets `genCfg.ggLevel`. |
| `--gen-density=<0..100>` | Sets `genCfg.stuffDensity`. |
| `--gen-perim=<0\|1>` | Sets `genCfg.perimeterBedrock`. |
| `--gen + --level` | Rejected at parse time (mutex). |

**Why a fresh algorithm instead of the original's GG filler stamps.**
The original's path takes per-theme `s1.tga..sN.tga` filler shapes
(black = empty, non-black = solid) and stamps them randomly onto the
canvas. We don't ship the theme TGA assets, so a faithful port has
nothing to stamp. The elliptical-blob generator produces a playable
attribute map (Air for ships to fly, Solid to hide behind, ground
floor for gravity) with no theme dependency. When theme TGAs land
later they paint the JPG visual layer on top; the cell shape is
what this header generates.

**Replay format bumped v1 → v2.** `ReplayHeader` re-purposes the 11
spare padding bytes from M5.4 for `(useGen u8, genLevel u8,
genDensity u8, genPerim u8, genSeed u32)`. v1 recordings cannot
play under the v2 reader (`hdr.version != 2` short-circuits the
open). When `useGen=1`, `levelDirLen` is forced to zero and the
header carries the generator config instead — playback rebuilds
the same level deterministically via the same generator path.

**Determinism contract**, pinned by `tou2d_proc_gen_test`:
- Same `(seed, ggLevel, stuffDensity, perimeterBedrock)` →
  byte-identical `attr[]` and `hp[]`.
- Distinct seeds → distinct outputs.
- Every `ggLevel ∈ {0..4}` yields both Air and Solid cells (no
  degenerate full-canvas fill in either direction).
- Pre-existing junk in the grid is wiped — generator always resets
  before fill.
- `density=0` produces strictly fewer solid cells than `density=100`.
- `perim=0` leaves the canvas corners as Air; `perim=1` writes
  `0xFF` bedrock there.
- Out-of-range bytes (`ggLevel=99`, `density=200`) are clamped, no
  UB.

**Smoke matrix** (record → play under `--gen`, all `--humans=1
--bots=3` deathmatch):

| Config                                  | Ticks | Verified | Mismatches |
|---|---|---|---|
| `--gen --gen-seed=42 --gen-level=1 --gen-density=80` | 300 | 300 | 0 |

Pre-M5.5 `ctest` had 131 tests; M5.5 adds the proc-gen unit test
for a new total of 132 — all green. Release build with
`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` clean.

#### M5.6 — New special weapon types (LANDED ✅, 2026-05-29)

Pre-M5.6 the second-weapon slot was hard-coded to the M3.3 Spread.
M5.6 generalises it: `WeaponLoadout::specialKind` selects one of four
catalogue entries; each carries its own (magazine, reload, cooldown,
bullets-per-shot, fan step, muzzle speed, ttl, per-bullet damage)
tuple via `kSpecialWeaponSpecs[]` in `DemoTypes.hpp`. The
`WeaponFireSystem` reads the spec at fire time instead of dispatching
on hard-coded constants — adding a 5th kind is now one row in the
table.

The four kinds:

| Kind     | weaponKind | mag | reload | cooldown | bullets | dmg/bullet | speed | ttl  | step    |
|----------|------------|-----|--------|----------|---------|------------|-------|------|---------|
| Spread   | 1          | 1   | 75     | 22       | 3       | 5          | 520   | 0.90 | 0.30 rad|
| Rapid    | 2          | 12  | 80     | 8        | 1       | 5          | 600   | 1.00 | 0       |
| Sniper   | 3          | 3   | 120    | 60       | 1       | 24         | 1100  | 1.60 | 0       |
| Quintet  | 4          | 1   | 90     | 30       | 5       | 4          | 560   | 0.95 | 0.175 rad|

Spread (kind 0) remains the default — pre-M5.6 saves and recordings
parse cleanly because the old `_pad` byte in the replay header and
the old `_pad0` in `WeaponLoadout` were both zero, which is exactly
`SpecialKind::Spread`.

**WeaponLoadout schema change.** Rename `spread* → special*` and
re-purpose `_pad0` as `specialKind`. POD stays 16 bytes:

```cpp
struct WeaponLoadout {
    std::uint16_t dumbfireAmmo, dumbfireReloadIn, dumbfireCooldown;
    std::uint8_t  specialKind;   // M5.6 — was _pad0
    std::uint8_t  _pad0;
    std::uint16_t specialAmmo, specialReloadIn, specialCooldown;
    std::uint16_t _pad1;
};
```

Same pattern in the v2 replay header — the `_pad` byte at offset 9
became `specialKind` (header still 32 B; v2 recordings made before
M5.6 read back as Spread, which matches their actual behaviour, so
the format version doesn't bump).

**CLI surface** (additions to main.cpp):

| Flag                        | Effect                                                                  |
|-----------------------------|-------------------------------------------------------------------------|
| `--special=spread`          | Default — pre-M5.6 behaviour, single-burst spread fan                  |
| `--special=rapid`           | High-fire-rate machine gun (12-round mag, 8-tick loader)                |
| `--special=sniper`          | Long-range high-damage straight bullet (3-round mag, 60-tick loader)    |
| `--special=quintet`         | 5-bullet narrow fan (single-burst mag, 90-tick reload)                  |
| `--special=<other>`         | Rejected at parse with exit 2                                           |

Other systems touched (rename pass + dispatch):

- `WeaponFireSystem::update` — dispatches on `ld.specialKind` via
  `specialSpecAt(...)`; one symmetric fan loop replaces the
  hard-coded 3-bullet spread block.
- `ShipLifecycleSystem` + `RoundRestartSystem` — preserve a ship's
  `specialKind` across respawn / round reset; only the counters
  reset.
- `HudSystem` — slot fields renamed `spread* → special*`; ammo row
  pulls mag size + glyph color from the spec table.
- `BulletShipCollisionSystem` — `sparkColorFor(weaponKind)` gives
  each kind a distinct impact-spark palette (yellow / orange / cyan /
  magenta / lime).
- `TouGame::setDefaultSpecialKind` — new setter; latched into every
  initial spawn.
- `ReplayPlayer::specialKind()` — accessor exposed so `--play` can
  override the cli `--special` from the recorded header.

**Determinism contract.** Same `(seed, gen config, specialKind)` →
byte-identical commit hash stream across record / replay. Verified
across all four kinds.

**Smoke matrix** (2026-05-29):

| Cmdline                                                   | ticks | verified | mismatches |
|-----------------------------------------------------------|-------|----------|------------|
| `--gen --gen-seed=42 --gen-level=1 --special=spread`       | 150   | 150      | 0          |
| `--gen --gen-seed=42 --gen-level=1 --special=rapid`        | 150   | 150      | 0          |
| `--gen --gen-seed=42 --gen-level=1 --special=sniper`       | 250   | 250      | 0          |
| `--gen --gen-seed=42 --gen-level=1 --special=quintet`      | 150   | 150      | 0          |
| `--special=plasma` (unknown token)                         | n/a   | n/a      | exit 2     |

Pre-M5.6 `ctest` had 132 tests; M5.6 adds `tou2d_specials_test`
pinning the enum order + spec-table non-degeneracy for a new total
of 133 — all green. Release build with
`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` clean.

#### M5.7 — Repair pickups + extra weapons + camera zoom (LANDED ✅, 2026-05-29)

Three bundled additions, one batch:

1. **Three more special weapons** extend the M5.6 catalogue to seven
   total: Heavy, Quad, Shotgun. Hooking a new kind is still one row
   in `kSpecialWeaponSpecs`.
2. **Repair pickup tiles** — a non-blocking `Attribute::Repair`
   variant of the terrain. Ship-vs-tile overlap consumes the tile,
   heals the ship by `kRepairHealAmount` (40), and cycles the
   ship's `WeaponLoadout::specialKind` to the next entry in the
   catalogue (`+1 mod kSpecialKindCount`). Matches the original
   TOU's repair-pad mechanic.
3. **Per-viewport zoom normalization** — when there's more than one
   human, each split-screen viewport scales `orthoHalfH` by the
   viewport's height fraction so a ship spans the same on-screen
   pixel count in single-player full-screen, 2-player horizontal
   split, AND 3/4-player 2×2 grid. Without this, 4-player ships
   were rendered at HALF size relative to the single-player baseline.

Seven special kinds (M5.7 additions in bold):

| Kind     | weaponKind | mag | reload | cooldown | bullets | dmg/bullet | speed | ttl  | step    |
|----------|------------|-----|--------|----------|---------|------------|-------|------|---------|
| Spread   | 1          | 1   | 75     | 22       | 3       | 5          | 520   | 0.90 | 0.30 rad|
| Rapid    | 2          | 12  | 80     | 8        | 1       | 5          | 600   | 1.00 | 0       |
| Sniper   | 3          | 3   | 120    | 60       | 1       | 24         | 1100  | 1.60 | 0       |
| Quintet  | 4          | 1   | 90     | 30       | 5       | 4          | 560   | 0.95 | 0.175 rad|
| **Heavy**    | **5**          | **4**   | **90**     | **35**       | **1**       | **20**         | **440**   | **2.00** | **0**       |
| **Quad**     | **6**          | **2**   | **80**     | **25**       | **4**       | **4**          | **560**   | **1.00** | **0.10 rad** |
| **Shotgun**  | **7**          | **1**   | **85**     | **30**       | **7**       | **3**          | **500**   | **0.65** | **0.18 rad** |

**Repair pickup contract.** `RepairPickupSystem` registers between
`TerrainCollisionSystem` and `WeaponFireSystem` so the freshly-
cycled weapon fires the same tick the ship grabs the tile. Reads
Transform + UserData; writes EntityStructural (for the
`Ship::currentHp` + `WeaponLoadout::specialKind` writes). AABB
overlap rule matches the existing collision half-extent (11 wu)
so the visual cue lines up with the consume moment. On consume:

```text
hp_after  = min(maxHp, hp_before + 40)
kind_after = (kind_before + 1) % kSpecialKindCount
ammo_after = specialSpecAt(kind_after).magazine    (+ zero both reload counters)
grid.clear(cx, cy)  // Repair → Air
emit AudioPlay{kSoundTileBreak}
emit particle dust burst
```

The procedural generator sprinkles `cfg.repairTileCount` random
Air cells per level (default 8 for fresh CLI runs; 0 for any
config left at struct-default — preserves pre-M5.7 test +
back-compat behaviour). The synthetic-arena fallback drops 8
fixed-position tiles at ±8 cells from the origin. Imported `.lev`
levels keep the pre-M5.7 posture (no repair tiles); migrating the
legacy attribute byte to Repair is out of scope.

**Replay v2 header** gains a `repairTileCount` byte at offset 26
(re-using the first byte of the old `_pad2`). No version bump —
pre-M5.7 v2 recordings stored a zero byte there, which now parses
as `repairTileCount=0`. For procedural levels that means "no
repair tiles," which exactly matches the pre-M5.7 generator's
output → identical commit hash stream → clean round-trip without
the recording author having opted in.

**Camera zoom normalization.** `CameraSystem::buildRenderFrame`
reads `effectiveOrthoHalfH() = orthoHalfH_ * viewportFor(0).height`.
`HudSystem::buildRenderFrame` reads the same so HUD anchors land
at each viewport's true TL corner. The world-units-per-pixel ratio
is now identical across 1 / 2 / 3-4 human layouts; ship sprites
render at constant on-screen pixel count.

**CLI additions** (main.cpp):

| Flag                        | Effect                                                                  |
|-----------------------------|-------------------------------------------------------------------------|
| `--special=heavy`           | Heavy 4-round mag, 90-tick reload, single 20-damage shell               |
| `--special=quad`            | 4-bullet narrow even fan, 80-tick reload                                |
| `--special=shotgun`         | 7-bullet wide fan at point-blank range, 85-tick reload                  |
| `--repair-tiles=N`          | 0..255 repair pickup tiles in the procedural level (default 8 when `--gen`)|

Pre-M5.6 `--special` tokens (`spread` / `rapid` / `sniper` /
`quintet`) all still work. `--repair-tiles=300` rejected with
exit 2 (out of range).

**Other systems touched:**

- `DemoTypes.hpp` — `Attribute::Repair = 3`,
  `TerrainGrid::setRepair`, `kRepairHealAmount`, three more
  catalogue entries.
- `ProceduralLevel.hpp` — `repairTileCount` (replaces `_pad`),
  determinism-preserving sprinkle pass.
- `RepairPickupSystem.{hpp,cpp}` — new system; ~140 LOC.
- `TouGame.{hpp,cpp}` — registers RepairPickupSystem; fans the
  tile-destroy callback to it; synthetic-arena fixed sprinkle.
- `CameraSystem.{hpp,cpp}` — `effectiveOrthoHalfH()`; projection +
  ortho size scale by viewport height.
- `HudSystem.cpp` — uses the effective half so HUD corners track
  per-layout.
- `Replay.{hpp,cpp}` — header `repairTileCount` byte; recorder
  writes from genConfig; player exposes via `genConfig()`.
- `main.cpp` — `--repair-tiles=N`, three new `--special` tokens,
  switch-statement coverage for the new SpecialKind values.

**Determinism contract.** Same
`(seed, gen config, specialKind, repairTileCount)` → byte-identical
commit hash stream across record / replay. Verified across the
new specials AND with repair tiles active.

**Smoke matrix** (2026-05-29):

| Cmdline                                                          | ticks | verified | mismatches |
|------------------------------------------------------------------|-------|----------|------------|
| `--gen --gen-seed=1234 --special=heavy --record/--play`           | 250   | 250      | 0          |
| `--gen --gen-seed=5678 --special=quad --record/--play`            | 200   | 200      | 0          |
| `--gen --gen-seed=5678 --special=shotgun --record/--play`         | 200   | 200      | 0          |
| `--gen --repair-tiles=0`                                          | 200   | n/a      | n/a        |
| `--gen --repair-tiles=32`                                         | 200   | n/a      | n/a        |
| `--humans=4 --bots=0 --gen --special=shotgun` (4-player camera)   | 200   | n/a      | n/a        |
| `--special=plasma` (unknown token)                                | n/a   | n/a      | exit 2     |
| `--repair-tiles=300` (out of range)                               | n/a   | n/a      | exit 2     |

Pre-M5.7 `ctest` had 133 tests; M5.7 adds `tou2d_repair_pickup_test`
and `tou2d_camera_zoom_test` for a new total of 135 — all green.
Release build with `-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` clean.

#### M5.8 — Final weapon batch + M5 closeout (LANDED ✅, 2026-05-29)

**M5 stretch goal closed.** Catalogue now ships ten special weapons
+ Dumbfire — eleven total. M5.8 demonstrates three engine
touchpaths that didn't exist before:

1. **Mine** — `muzzleSpeed = 0` in the spec table is the
   drop-in-place signal. `WeaponFireSystem::spawnBullet` skips the
   muzzle-offset push AND velocity inheritance, dropping the bullet
   at the ship's position with zero forward velocity. Reuses every
   downstream system (ProjectileSystem ticks the ttl; existing
   collision systems handle a ship-on-mine hit). Long ttl (3.5 s)
   gives the trap real persistence.
2. **Bouncer** — `BulletTerrainSystem` extension. On solid-cell hit
   for a Bouncer-kind bullet (`weaponKind == 9`, `bouncesLeft > 0`,
   non-bedrock), the bullet reflects instead of destroying: the
   "back" cell on each axis (`backX = cx ± 1` based on velocity
   direction) is probed; if it's air, that axis's velocity flips and
   the bullet is nudged back into the air cell to avoid a re-collision
   next tick. Corner hits (both neighbors solid) flip both axes —
   straight back. Per-bounce `kBouncerDamping = 0.9` keeps the shot
   meaningfully fast across its 3-bounce budget (≈73% of muzzle speed
   at the final bounce). `bouncesLeft` decrements per hit; on zero
   the normal destroy path takes over.
3. **Homer** — new `BulletHomingSystem` (header-only-style; ~140 LOC).
   Walks Homer-kind bullets (`weaponKind == 10`), finds the nearest
   enemy ship (slot != `ownerSlot`, skips DisabledTag), computes the
   shortest signed angular delta between current velocity heading
   and target bearing, and rotates by at most `kHomerTurnPerTickRad`
   (0.32 rad ≈ 18°/tick) so dodging is possible. Speed is preserved
   across the rotation. Pass 1 of `update()` flat-collects all ship
   (x, y, slot) into a `kMaxPlayerSlots`-sized array so the bullet
   loop is `O(bullets * shipCount)` on a hot vector instead of
   re-walking ship chunks per bullet. Wave-ordering: between
   `weaponFire` and `projectile` so a freshly-spawned Homer locks on
   this tick.

**Bullet POD grew 8 → 12 bytes** for the `bouncesLeft` byte (+ 3 pad).
Game-side POD; not serialized; nothing pins the legacy size. The
`SpecialWeaponSpec` slot that was `_pad0` is now `bouncesLeft` — same
24-byte layout, repurposed.

Ten special kinds (M5.8 additions in bold):

| Kind         | weaponKind | mag | reload | cool | bullets | dmg | bounces | speed | ttl  | step    |
|--------------|------------|-----|--------|------|---------|-----|---------|-------|------|---------|
| Spread       | 1          | 1   | 75     | 22   | 3       | 5   | 0       | 520   | 0.90 | 0.30 rad|
| Rapid        | 2          | 12  | 80     | 8    | 1       | 5   | 0       | 600   | 1.00 | 0       |
| Sniper       | 3          | 3   | 120    | 60   | 1       | 24  | 0       | 1100  | 1.60 | 0       |
| Quintet      | 4          | 1   | 90     | 30   | 5       | 4   | 0       | 560   | 0.95 | 0.175 rad|
| Heavy        | 5          | 4   | 90     | 35   | 1       | 20  | 0       | 440   | 2.00 | 0       |
| Quad         | 6          | 2   | 80     | 25   | 4       | 4   | 0       | 560   | 1.00 | 0.10 rad |
| Shotgun      | 7          | 1   | 85     | 30   | 7       | 3   | 0       | 500   | 0.65 | 0.18 rad |
| **Mine**     | **8**      | **4**| **110**| **30**| **1**  | **28**| **0** | **0**     | **3.50** | **0**       |
| **Bouncer**  | **9**      | **2**| **75** | **22**| **1**  | **10**| **3** | **480**   | **1.80** | **0**       |
| **Homer**    | **10**     | **3**| **130**| **50**| **1**  | **14**| **0** | **320**   | **2.40** | **0**       |

**CLI additions** (main.cpp):

| Token            | Effect                                                              |
|------------------|---------------------------------------------------------------------|
| `--special=mine` | Mine drop-in-place; sits at ship for 3.5 s; 28 damage on contact    |
| `--special=bouncer` | Reflects 3 times off solid terrain; ship hit / bedrock destroy   |
| `--special=homer` | Steers toward nearest enemy at ≤18°/tick; slow speed; long ttl    |

**Smoke matrix** (2026-05-29):

| Cmdline                                                          | ticks | verified | mismatches |
|------------------------------------------------------------------|-------|----------|------------|
| `--gen --special=mine --record/--play`                            | 150   | 150      | 0          |
| `--gen --special=bouncer --record/--play`                         | 150   | 150      | 0          |
| `--gen --special=homer --record/--play`                           | 150   | 150      | 0          |
| `200 --special=mine` (headless)                                   | 200   | n/a      | n/a        |
| `200 --special=bouncer` (headless)                                | 200   | n/a      | n/a        |
| `200 --special=homer` (headless)                                  | 200   | n/a      | n/a        |
| `--special=plasma` (rejected, updated error message)              | n/a   | n/a      | exit 2     |

Pre-M5.8 `ctest` had 135 tests; M5.8 adds `tou2d_weapon_mechanics_test`
for a new total of 136 — all green. Release build with
`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` clean.

**Files touched:**

- `DemoTypes.hpp` — three catalogue entries, `Bullet::bouncesLeft`,
  `SpecialWeaponSpec::bouncesLeft` (renamed from `_pad0`),
  `kHomerTurnPerTickRad`, `kBouncerDamping`.
- `WeaponFireSystem.cpp` — `spawnBullet` accepts `bouncesLeft`; the
  `speed == 0` branch suppresses muzzle offset + velocity inheritance.
- `BulletTerrainSystem.cpp` — pre-destroy reflect branch for Bouncer-
  kind bullets; back-cell axis probe; per-bounce damping; bullet
  nudged back into the air cell.
- `BulletHomingSystem.{hpp,cpp}` — new system.
- `TouGame.cpp` — registers BulletHomingSystem between weaponFire
  and projectile.
- `CMakeLists.txt` — adds `BulletHomingSystem.cpp` to tou2d_core.
- `main.cpp` — three new `--special=` tokens; switch + log line
  coverage.
- `tests/tou2d_specials_test.cpp` — extends enum/catalogue assertions.
- `tests/tou2d_weapon_mechanics_test.cpp` — new test pinning the
  Bouncer reflect math and Homer angular-step + speed-preservation
  contract.

**M5 ACCEPTANCE STATUS: COMPLETE.** Replay round-trips bit-identical
across every M5.4–M5.8 feature combination; the catalogue stretch
goal is closed. Future weapon additions (rockets, mines variants,
homers, etc.) lay against the same `kSpecialWeaponSpecs`
table-driven path established here. Out-of-band weapon mechanics
(self-effects like Teleporter, terrain-spawn like Brickwall) remain
deferred to a post-v1 milestone — they require a non-trivial new
fire path beyond `spawnBullet`.

### Out of scope for v1
- Split-screen (deferred per § 1).
- Level editor (the source-asset workflow IS the editor).
- Network multiplayer (the original was local-only too).
- More than 4 humans (the original supported 4; we match).
- More than 63 bots (the original's cap; we may go higher trivially but it's not the proof point).

---

## 8. Threadmaxx-side risks to watch

Each item here is a known engine subsystem that hasn't been exercised in this shape yet. Listing so the relevant milestone's batch lead has the question top-of-mind.

| Risk | Where it bites | Mitigation |
|---|---|---|
| 2D render contract | `vulkan_renderer` is 3D-only today | Milestone 1 pick: extend vs. parallel. Decision deferred to the batch that touches it. |
| Mass particle entity count | Milestone 3 explosions easily spawn thousands of `Particle` entities per frame | `forEachChunk<Particle, Transform>` + the auto `instances` lane handles this — already proven at scale in rpg_demo. |
| Tile-grid as ECS entities | 500×500 tiles = 250 k entities. ArchetypeChunk can handle it, but stitched-view rebuilds get expensive on mutation | Avoid stitched view on the hot path. Iterate via `forEachChunk` only. Possibly: keep tiles in a flat `std::vector<uint8_t>` outside the ECS, only promote to entities on damage. |
| `UserComponent<T>` size budget | `Bullet`, `Particle` may want 32+ bytes; multiplied by thousands of entities, that's MB of dense storage | Aim for ≤ 16 bytes per Particle. Pack flags into bitfields. |
| Audio thread integration | miniaudio runs its own callback thread; AudioTriggerSystem must enqueue, not block | Use `EventChannel<AudioPlay>` — the typed event channel is already MPSC-safe. miniaudio drains on its own thread. |
| Input → multi-player on one keyboard | 7 keys × 4 players = 28 keys; key-ghosting on cheap keyboards | Document a known-good rollover-friendly keyboard. Joystick / gamepad support is stretch. |
| Determinism with floats | The original used integer / fixed-point physics on Win9x | Stick to `float` but pin `-ffloat-store` / `-msse2 -mfpmath=sse` (already the engine default on Linux). Document that replay determinism is single-platform. |

---

## 9. What to commit when

Per the standing project commit policy (don't autonomously commit; user authorizes per artifact):

- **This document** (`TOU_PLAN.md` + the `examples/tou2d/` directory containing it) — ready to commit on user authorization. No code yet.
- **Milestone 1** — first code-bearing batch. Bundles renderer choice + ship-thrust loop. Single PR / commit.
- **Milestone 2** — second batch. Bundles terrain + Tier 1 importer.
- Each subsequent milestone — its own batch.

Per the threadmaxx convention (see `CLAUDE.md` § "When adding a new public symbol"), if any milestone exposes a new public engine knob, that knob lands with a `doc/performance_tuning.md` entry in the same batch. No new public surface is currently planned — the tou2d binary is example-side only.

---

## 10. Open questions before milestone 1 starts

Listed so the next session has them surfaced.

1. **2D render backend choice** — extend `threadmaxx_vk` or new `threadmaxx_2d`? Affects build matrix + CMake.
2. **Audio backend choice** — `miniaudio` (single-header, MIT) is the obvious pick. Confirm or substitute.
3. **Image loader choice** — `stb_image` (single-header, public domain) is the obvious pick for JPG/TGA/PNG. Confirm or substitute.
4. **Whether to track `TOU/` in git** — currently visible in the working tree but the rationale above says don't redistribute. Add to `.gitignore`?
5. **License footer on imported assets** — when Tier 1 / Tier 2 outputs land in `assets/levels/`, do they get a header note "imported from user's TOU install; original © GigaMess 2002"? Probably yes.

These are scope decisions, not technical blockers. Answer them in the milestone 1 kickoff conversation, not here.

---

*This plan lives at `examples/tou2d/TOU_PLAN.md` and is the source of truth for the project's design. The ChatGPT design notes at `tou_threadmaxx_design_notes.md` are the inspirational document; where the two disagree, this one wins.*
