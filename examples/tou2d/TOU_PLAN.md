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

### Milestone 6 — Complete GUI / UX polish (PLANNED, NEXT)
- M6.0 — engine prereqs: TTF font (drop-in swappable, baked via `stb_truetype.h`), UI compositor at `RenderPass::Overlay`, `KeyMap` action indirection.
- M6.1 — `UISystem` state machine + main menu (keyboard navigation, focus model, safe defaults).
- M6.2 — Match / level setup screen (every gameplay knob the CLI exposes today, exposed graphically + benchmark presets row).
- M6.3 — Ship / player slot assignment (2-4 humans, explicit ship + color + tag picks).
- M6.4 — Pause menu wired to `Engine::setPaused` (deterministic resume; replay-safe).
- M6.5 — Options menu (Video / Audio / Controls / Gameplay / Accessibility / Benchmark) + persistent `settings.dat`.
- M6.6 — Results / scoreboard screen + rematch flow.
- M6.7 — HUD polish: thicker bars, warning indicators, photosensitive-mode flash cap.
- M6.8 — Notification / dialog layer (toast channel; pickup feed, kill feed, system messages).
- M6.9 — Debug / benchmark overlay (FPS, entity count, commitHash, top-3 systems, world seed).
- M6.10 — Flow polish + acceptance pass (launch-to-gameplay-in-1s, universal Esc, headless menu replay).
- **Acceptance**: full M6 acceptance checklist (see § 7.M6 below) — main menu / setup / options / pause / HUD / results / benchmarks all functional and budget-meeting; replay determinism preserved across menu / pause cycles; CLI direct-jump path unchanged.

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

### Milestone 6 — Complete GUI / UX polish (PLANNED, NEXT)

**Intent.** Turn the gameplay-complete demo into a shippable PC game
experience: real main menu, level / match setup, options menus,
pause menu, ship loadout, results screen, polished HUD, clean input
navigation, benchmark / stress presets, smooth transitions between
menus and gameplay. The simulation contract (deterministic step,
replay capture, per-archetype hash) stays untouched — every menu
lives at the example layer (no public engine surface changes).

**Design heuristic.** Late-90s / early-2000s PC game UI, modernised
for readability. Fast to start, fast to restart, clear under local-MP
chaos, readable during explosions and weapon spam. **Not decorative —
practical, fast, testable.** Every M6 batch ships with at least one
headless smoke + (where relevant) a deterministic replay round-trip,
same gate every prior milestone passed.

**Sub-batch order** (each batch lands independently; no batch blocks
gameplay):

| Batch | Title | Depends on | Engine-side prereq? |
|-------|-------|------------|---------------------|
| M6.0  | Engine prereqs — TTF font (drop-in) + UI compositor + key-action layer | — | YES (font + key-map) |
| M6.0b | Vulkan overlay path + UISystem skeleton + CPU compositor | M6.0 | no |
| M6.1  | UI state machine + main menu (LANDED 2026-05-29) | M6.0b | no |
| M6.2  | Match / level setup screen (LANDED 2026-05-29) | M6.1 | no |
| M6.3  | Ship / player slot assignment screen (LANDED 2026-05-30) | M6.1 | no |
| M6.4  | Pause menu (in-game) (LANDED 2026-05-29) | M6.1 | no (uses `Engine::setPaused`) |
| M6.5  | Options menu + persistence (LANDED 2026-05-30) | M6.0, M6.1 | YES (settings POD) |
| M6.6  | Results / scoreboard screen (LANDED 2026-05-30) | M6.1 | no |
| M6.7  | HUD polish pass + M6.5 accessibility application (LANDED 2026-05-30) | M6.0 | no |
| M6.8  | Notification / dialog layer (LANDED 2026-05-30) | M6.0 | no |
| M6.9  | Debug / benchmark overlay (LANDED 2026-05-30) | M6.0 | no |
| M6.9b | Overlay completeness pass — game-state rows + render budget (LANDED 2026-05-30) | M6.9 | no |
| M6.10 | Flow polish + acceptance pass (LANDED 2026-05-30) | all prior | no |

#### M6.0 — Engine prereqs (font + UI compositor + key-action map)

Three foundation pieces M6.1+ all need. Each is example-layer; nothing
in `include/threadmaxx/` changes.

1. **TTF font atlas, drop-in swappable.** Bake any TTF/OTF to a glyph
   atlas at startup via `stb_truetype.h` (single-header, public domain,
   pulled in alongside `stb_image`). The font file path is a resource
   loaded through `ResourceRegistry` — swap by replacing
   `assets/ui/font.ttf` (or pointing the loader at a different file via
   `tou2d::ui::FontConfig::ttfPath`). v1 default: a permissively
   licensed file shipped in-tree (DejaVu Sans, JetBrains Mono, or Inter
   — SIL OFL / Apache-2.0 / Bitstream-Vera). The atlas type:

   ```cpp
   struct GlyphMetrics {
       std::int16_t  u0, v0, u1, v1;   // atlas pixel rect
       std::int16_t  xoff, yoff;       // pen-relative draw offset
       std::int16_t  xadvance;         // 26.6 → integer pixels
       std::uint16_t codepoint;        // sparse — supports non-ASCII
   };
   struct FontAtlas {
       int                              pixelSize;      // baked size
       int                              atlasW, atlasH;
       int                              ascent, descent, lineGap;
       std::vector<std::byte>           pixels;         // R8 alpha
       std::vector<GlyphMetrics>        glyphs;         // sorted by codepoint
       std::unordered_map<std::uint32_t, std::uint32_t> indexByCp;  // O(1) lookup
   };
   ```

   `tou2d::ui::bakeFont(ttfBytes, FontConfig{pixelSize, codepoints})`
   produces a `FontAtlas` (one bake per UI-scale tier — M6.5's slider
   gets clean glyphs at 75/100/125/150% instead of nearest-neighbour
   smearing). `tou2d::ui::textPrintf(builder, atlas, x, y, color, fmt,
   ...)` walks the codepoint stream, looks up `GlyphMetrics`, issues
   one `DrawItem` per glyph against the `RenderPass::Overlay` lane with
   per-glyph UV rect. `tou2d::ui::measureText(atlas, text)` returns the
   pixel bbox + baseline. Proportional metrics + horizontal advance;
   kerning pairs deferred (stb_truetype gives them cheaply — opt-in if
   labels look loose). No SDF, no hinting toggle, no subpixel
   positioning — this is HUD typography, not document rendering.

   **Drop-in contract**: any TTF/OTF that defines the ASCII range
   (0x20..0x7E) loads and renders without code changes. Codepoint
   coverage beyond ASCII is opt-in via `FontConfig::codepoints`
   (default: 0x20..0x7E + a handful of HUD symbols). Atlas dims are
   computed from the input; no hard-coded grid pitch.

2. **UI compositor layer.** A lightweight `RenderFrameBuilder` slice
   that lives at NDC z-order ABOVE the gameplay layer. Renderer-side,
   the existing `RenderPass::Overlay` lane is the obvious home — every
   UI quad goes there; depth test off; alpha blending on; reset every
   tick. No retained-mode tree; the immediate-mode model fits
   threadmaxx's "rebuild every tick from systems" rhythm.

3. **Key-action indirection.** `InputSystem` today reads GLFW keys
   directly and dispatches per-slot. M6.0 inserts a `KeyMap` POD between
   GLFW and the action enum:

   ```cpp
   enum class Action : std::uint8_t {
       Thrust, RotateLeft, RotateRight, FireDumb, FireSpecial,
       Pause, UiUp, UiDown, UiLeft, UiRight, UiAccept, UiCancel,
       kActionCount
   };
   struct KeyMap {                       // 4 player slots * Action::kActionCount
       std::uint16_t binding[kMaxHumans][static_cast<std::size_t>(Action::kActionCount)];
   };
   ```

   `InputSystem` reads `KeyMap` instead of hard-coded scancodes; M6.5's
   Controls screen mutates `KeyMap` in place. Defaults match the
   original TOU bindings so existing keyboards keep working.

**Acceptance**: `tou2d::ui::textPrintf` renders "HELLO" cleanly at
arbitrary screen coords across all 4 viewports; existing gameplay
input is bit-identical (rebind the defaults and the replay determinism
test still round-trips); ctest unchanged at 137+ (new
`tou2d_font_atlas_test`, `tou2d_keymap_default_binding_test`).

#### M6.1 — UI state machine + main menu (LANDED 2026-05-29)

**Outcome.** `UISystem` registered before `InputSystem`; owns the
`UIScreen current` state + a per-screen `MenuRow` table + a focus
cursor. MainMenu rows match the §M6 spec (Continue greyed, Single
Match, Level Setup stub, Options stub, Benchmark stub, Credits, Quit).
Credits is a single-`Back`-row sub-screen. The four stub rows log to
stderr; LevelSetup/Options/Benchmark land in M6.2/M6.5 respectively.

**Input.** main.cpp polls GLFW directly for the six `Ui*` actions on
slot 0 of the default `KeyMap`, edge-detects RISING transitions, and
calls `moveFocus(±1)` / `acceptFocused()` / `setCurrent(MainMenu)`
(UiCancel pop-back from sub-screens). `UISystem::update` stays empty
so the wave scheduler treats it as a no-op; the state mutates outside
the engine step.

**Pause coupling.** When `menuActive()`, main.cpp calls
`engine.setPaused(true)`; transition back to `UIScreen::None` unpauses.
`engine.step()` is a no-op while paused, so ships freeze in-place
without an explicit InputSystem swallow.

**CLI bypass.** `argc > 1` → bypass menu; gameplay starts immediately
on `UIScreen::None` (preserves headless smoke + replay + bench
behaviour). `argc == 1` → land on MainMenu paused; "Single Match"
dismisses the menu and unfreezes the existing world (no re-spawn —
ships were already seeded in `TouGame::onSetup` with the M5.1
defaults).

**Tests.**
* `tou2d_menu_navigation_test` — pins the MainMenu row table, focus
  init lands on first enabled, moveFocus wrap, moveFocus skips
  disabled, Quit → `pendingQuit()`, Credits → Back round-trip.
* Existing `tou2d_uiscreen_state_machine_test` covers the
  `UIScreenChanged` emit + same-screen no-op invariants.
* Headless bounded run (`./threadmaxx_tou2d 200`) bypasses menu and
  produces the same final ship pos as M6.0b.
* Record/replay round-trip at 150 ticks: 0 hash mismatches.

**Spec reference (preserved for context).**

**`UISystem`** — new `ISystem` that owns the screen state machine:

```cpp
enum class UIScreen : std::uint8_t {
    None,         // gameplay-only (legacy path)
    MainMenu,
    MatchSetup,
    PlayerSetup,
    Options,
    Pause,
    Results,
    Credits,
};
```

Wave ordering: BEFORE `InputSystem` so a screen-active tick can swallow
gameplay input. `UISystem::update` reads the action enum (M6.0), routes
to per-screen handlers, emits `UIScreenChanged{from, to}` events via
the typed event channel for any system that wants to react.

**Main menu rows** (matches the user's spec):
- `Continue / Resume` (greyed if no saved match)
- `Single Match`
- `Level Setup` (jumps to M6.2)
- `Options` (jumps to M6.5)
- `Benchmark / Stress` (preset launcher — see M6.5)
- `Credits / About`
- `Quit`

**Focus model.** Single-focus index per screen; `UiUp` / `UiDown` move;
`UiAccept` fires; `UiCancel` pops back. Mouse hover sets focus
(optional polish — keyboard is the contract).

**Safe defaults on first launch.** When no settings file exists,
`UISystem` lands on MainMenu and offers "Quick Start" (= 1 human + 3
bots, jungle level) as the top option. The legacy `--humans` / `--bots`
CLI flags still work — they bypass MainMenu and jump straight to a
match, matching today's behaviour for headless smoke / replay use.

**Acceptance**: launching without CLI args shows MainMenu; navigating
with arrows + Enter into "Single Match" → "Start" lands in gameplay
identical to today's `--humans=1 --bots=3 --level <default>`; headless
smoke (`./threadmaxx_tou2d 200`) still bypasses the menu via the
existing CLI-direct-jump path (no regression).

#### M6.2 — Match / level setup screen (LANDED 2026-05-29)

**`MatchSetup` POD as the single source of truth.** New header
`examples/tou2d/MatchSetup.hpp` defines a 16-byte POD carrying every
gameplay knob the CLI exposes today: humans, bots, mode, special
weapon, useGen flag, full `ProceduralLevelConfig` (seed, ggLevel,
density, perim, repair tiles). Defaults reproduce the M5.1 "no CLI
args" gameplay shape.

**Single fan-out via `TouGame::setMatchSetup`.** Both the CLI parse
path and the future menu Start path call this one method; it routes
each field to the existing per-knob setter (`setPlayerCounts`,
`setMatchMode`, `setDefaultSpecialKind`, `setGenerationConfig`). The
CLI's `--level <path>` knob stays separate because the menu has no
directory enumerator (CLI-only scope for v1).

**Determinism contract pinned today.** `main.cpp` now assembles a
`MatchSetup` from CLI flags BEFORE calling `setMatchSetup`. Bounded
`200`-tick smoke produces the identical final ship position as M6.1
(`-42.00, -43.25`); record/replay round-trip at 150 ticks with
`--gen --gen-seed=42 --gen-level=1 --special=sniper` reports **0 hash
mismatches** — proves the refactor is bit-exact against the pre-M6.2
CLI flow.

**Knob scope (10 scrollers + 2 action rows).** The MatchSetup screen
ships with 12 rows in this order:

| # | Row | Domain |
|---|-----|--------|
| 0 | Humans | 1..`kMaxHumans` (wrap) |
| 1 | Bots | 0..`kMaxBots` (wrap) |
| 2 | Mode | Deathmatch / LastShipStanding |
| 3 | Special | 10 `SpecialKind` values (wrap) |
| 4 | Procedural | Off / On |
| 5 | Gen seed | 8-preset list (0, 1, 2, 42, 1234, 0xCAFEF00D, 0xFEED, 0xDEADBEEF) |
| 6 | Gen size | `ggLevel` 0..4 (wrap) |
| 7 | Gen density | 0..100 in steps of 10 (wrap) |
| 8 | Gen perimeter | Off / On |
| 9 | Repair tiles | 0..32 in steps of 4 (wrap) |
| 10 | Start match | Action — sets `pendingStartMatch_`, dismisses menu |
| 11 | Back | Action — pops to MainMenu |

The "Gravity / Wind / Weapon table / Match timer / Score limit /
Respawn rules / Damage scale / Camera mode / Stress preset" rows
from the original M6.2 plan are deferred: each requires a *new* CLI
flag (none of those knobs exist today). Adding them is one row +
one CLI flag + one apply hook each — the row-table infrastructure
here supports it directly.

**UISystem extensions (`MenuRowKind`, scroller cycle).** `MenuRow`
gained a `kind` field (Action / Scroller) and a `scrollerKnob`
binding. Three new public methods:
- `cycleFocused(±N)` — UiLeft/UiRight dispatch; mutates the bound
  knob on `matchSetup()`; no-op on Action rows.
- `formatRow(rowIdx, buf, bufN)` — paint helper; emits
  "`<label>: <value>`" for scrollers and the static label for actions.
- `matchSetup()` — mutable reference to the working POD.

`acceptFocused` grew arms for `MenuAction::LevelSetup` (jumps to
`UIScreen::MatchSetup`) and `MenuAction::StartMatch` (flips
`pendingStartMatch_` sticky + transitions to `UIScreen::None`).

**Host integration (`main.cpp`).** Frame-loop input dispatch now
pumps UiLeft/UiRight → `cycleFocused(±1)`. After each frame the
host checks `pendingStartMatch()`; for this batch the host LOGS the
chosen `MatchSetup` to stdout and clears the flag (the engine
restart-with-new-settings wiring lands in M6.4 alongside "Restart
match" — both need the same `engine.shutdown` + `setMatchSetup` +
`engine.initialize` sequence, so they share machinery). The menu
dismissal via `setCurrent(None)` unfreezes the simulation through
the existing pause-on-menuActive bind — the match runs with whatever
`MatchSetup` was applied at startup (today CLI defaults; M6.4 will
apply the menu's edits).

**Pre-seeding.** The UI's working `MatchSetup` is initialised from
the CLI's resolved values, so a menu opened mid-CLI-run reflects
the launch config (e.g. `tou2d --bots=10` then opening MatchSetup
shows Bots: 10 already selected).

**Test (`tou2d_match_setup_test`).** Pins the row table identity
(12 rows, scroller knob order, action row labels), focus initial
position, scroller cycle wraps (Humans / Bots / Mode / Special /
UseGen / GenSeed presets), cycle-on-Action-no-op invariant,
StartMatch dispatch (pendingStartMatch sticky + setCurrent(None)),
BackToMain dispatch + focus reset, MainMenu "Level Setup" routing,
formatRow output ("Humans: 1", "Mode: Deathmatch", "Start match",
"Back"), and the **byte-equality determinism contract**: a
`MatchSetup` reached by `cycleFocused`-driven menu navigation is
`memcmp`-equal to one built by direct field assignment to the same
values.

**Pre-M6.2 `ctest` had 141 tests; M6.2 adds `tou2d_match_setup_test`
for a new total of 142 — all green.** Release build with
`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` clean. Headless smoke at 200
ticks and gen-mode record/replay round-trip both bit-identical to
pre-M6.2.

**Still TBD** (deferred follow-ups, NOT blockers for M6.2 acceptance):
- Engine restart-with-new-settings for `StartMatch` apply. Originally
  slated to land with M6.4 alongside `RestartMatch`, but M6.4 shipped
  the Pause UI/flag/test infrastructure with both Start and Restart
  deferred to a focused engine-restart follow-up batch — that one
  primitive lights up both arms.
- Gravity / Wind / Weapon table / Match timer / Score limit /
  Respawn rules / Damage scale / Camera mode rows — each needs a
  new CLI flag first; row infrastructure is ready.
- Level path enumerator (CLI's `--level` stays CLI-only until a
  filesystem walker lands).
- Stress preset row — depends on M6.5's preset infrastructure
  (the existing CLI doesn't expose presets).

#### M6.3 — Ship / player slot assignment screen (LANDED 2026-05-30)

**Wired surface.** New `UIScreen::PlayerSetup` with a slot-major row
table: for each of slots 0..3, six scroller rows (Tag c1 / Tag c2 /
Tag c3 / Role / Ship / Palette) followed by a trailing Back row. 25
rows total. `MatchSetup` row 10 ("Players...") jumps to the new
screen via `MenuAction::PlayerSetup`; the screen's `Back` row routes
back to `UIScreen::MatchSetup` (not `MainMenu`) so the back-row UX
returns the user to where they came from.

**Data plumbing.**
- New 8-byte `PlayerSlotSetup` POD (`MatchSetup.hpp`): `tag[3]`
  (A-Z + space; all-spaces means "auto"), `role` (0=Auto, 1=Human,
  2=Bot), `shipKindIdx` (0xFF=Auto = use `kAtlasSeeds[slot%4]`),
  `paletteIdx` (0xFF=Auto = use `slot%4`).
- `MatchSetup` extends with `std::array<PlayerSlotSetup, 4>
  playerSlots`. Static-assert updated to `16 + 4*8 = 48` bytes.
- `MenuRow` gains a `std::uint8_t slotIdx` field — meaningful only
  for the new per-slot knobs (`SlotTagChar0..2` / `SlotRole` /
  `SlotShip` / `SlotPalette`). Global knobs ignore it; Action rows
  ignore it.
- `cycleKnob_` / `formatKnobValue_` signatures refactored to take
  `slotIdx` so per-slot scrollers route into the right
  `playerSlots[slot]` cell.
- `TouGame::setMatchSetup` copies `playerSlots` into a member array;
  `onSetup`'s spawn loop resolves per-slot ship / palette / isBot
  from the overrides, falling through to the pre-M6.3 auto-cycle
  whenever an override holds its sentinel.

**Determinism contract.** Defaults on every override field are the
all-sentinel state (`tag=all-spaces`, `role=0`, `shipKindIdx=0xFF`,
`paletteIdx=0xFF`). A default-init `MatchSetup` is therefore
bytewise equal to the UISystem's working `MatchSetup` before any
edits — pinned by `tou2d_player_setup_test`'s "default MatchSetup
bytewise equal" block. The CLI path (which never touches
`playerSlots`) is determinism-equivalent to a menu run with no slot
edits. Verified empirically: 200-tick smoke (`./threadmaxx_tou2d
200`) final ship pos `(-42.00, -43.25, 0.00)` — bit-identical to
M6.4.

**Test (`tou2d_player_setup_test`).** Pins: the MatchSetup
"Players..." row routing to PlayerSetup; the 25-row PlayerSetup
table in slot-major order with per-row `slotIdx`; the tag-char 27-
position alphabet wrap; the role tristate (Auto/Human/Bot); the
ship-kind (1 + N) cycle with Auto sentinel; the palette (1 + 4)
cycle with Auto sentinel; per-slot write isolation (cycling slot 1
leaves slots 0/2/3 alone); `formatRow` rendering ("Slot N <field>:
<value>" with `_` standing in for a blank tag char); `BackToMain`
on PlayerSetup routing to MatchSetup (not MainMenu); and the
all-sentinel default-MatchSetup bytewise contract.

**Acceptance**: 2-human + 2-bot setup with explicit ship picks works;
the same setup via CLI flags (`--humans=2 --bots=2`) produces an
identical match (the menu just exposes per-slot overrides; defaults
match the auto-cycle). Verified: pre-M6.3 ctest had 144 tests; M6.3
adds `tou2d_player_setup_test` for a new total of 145 — all green.
Warnings-as-errors build clean.

**Still TBD** (deferred follow-ups, NOT blockers for M6.3 acceptance):
- HUD wiring for the resolved per-slot `tag` string. M6.3 stores the
  override on `MatchSetup`; `TouGame` consumes it for spawn-time
  decisions but doesn't yet plumb it through to `HudSystem`'s score
  display. Best landed as a focused HUD touch-up batch alongside the
  M6.7 HUD polish pass.
- Input device picker per slot is implicit today (slot index = key-
  binding row). Will become meaningful only if M6.5 (or later)
  adds remappable per-player keyboard layouts beyond the existing
  hand-rolled P1/P2/P3/P4 rows.
- "AI fill" toggle per empty slot is functionally subsumed by the
  `numHumans` / `numBots` knobs on MatchSetup plus the per-slot
  `role` override added in M6.3. A dedicated single-toggle row was
  judged redundant for v1.

#### M6.4 — Pause menu (LANDED 2026-05-29)

**Wired surface.** `UIScreen::Pause` is the in-game pause menu.
`MenuAction::Resume`, `MenuAction::RestartMatch`, and
`MenuAction::ReturnToMainMenu` are the three new enumerators; existing
`Options`, `LevelSetup`, and `Quit` arms are reused. The screen owns
six rows in fixed on-screen order: Resume, Restart match, Options,
Level setup, Return to main menu, Quit. Every row is enabled —
Continue-greyed is a MainMenu-only posture (the placeholder for the
engine-restart-with-MatchSetup follow-up; see "Still TBD" below).

**Pause entry.** `Action::Pause` (default `Esc`) is polled as a
seventh edge slot in main.cpp's `UiKeyEdges` (alongside the M6.1 six
nav slots). On rising edge AND `!menuActive()` the host calls
`ui->setCurrent(UIScreen::Pause)`; the menuActive→`engine.setPaused`
bind from M6.1 freezes the sim on the next frame. The renderer keeps
re-publishing the last submitted frame for free (no explicit snapshot
capture needed — `engine.step()` is a no-op while paused, so the back
frame buffer stays untouched and `submitInterpolatedFrame` keeps it
on screen).

**Pause exit.** Three paths converge on the same outcome
(`setCurrent(UIScreen::None)` → `menuActive()` flips false → engine
unpauses next frame):
- `MenuAction::Resume` accept on the Resume row.
- `UiCancel` (also `Esc`) from inside Pause. Pause and UiCancel share
  the Escape key — the if/else-if dispatch in main.cpp routes
  gameplay→Pause via the Pause edge and Pause→gameplay via the
  UiCancel edge without double-firing on the same key press.
- Pressing the Pause key in gameplay is rejected by the
  `!menuActive()` gate while a menu is up, so Esc-from-Pause goes
  through the UiCancel arm.

**Replay paused-skip (one-line — actually four).** The host frame
loop reads `engine.paused()` after the per-frame setPaused bind
commits and gates four call sites on `!framePaused`:
1. `replayPlayer.advance()` — paused frames consume no input from
   the recorded stream.
2. Per-slot `readKeyboardSlot` sampling — only run when the frame
   will actually step.
3. `replayRecorder.recordTick(...)` — paused frames emit nothing.
4. The post-step hash-mismatch check — pinned-to-non-paused so the
   `engine.stats().commitHash` we compare against was actually
   advanced by step().

Record + replay across pause/unpause are bit-identical to an unpaused
match where the pause never elapsed. The record/replay 150-tick
round-trip (with `--gen --gen-seed=42 --gen-level=1 --special=sniper`,
no actual pause exercised since the smoke is non-interactive)
confirms **0 hash mismatches** — the gate is pure non-paused-frame
no-op when the engine never pauses.

**MenuAction posture for the new arms.**
- `Resume` — `setCurrent(UIScreen::None)`, no sticky flag (the
  menuActive bind is the entire side effect).
- `RestartMatch` — sets `pendingRestartMatch_` sticky flag +
  `setCurrent(UIScreen::None)`. Host drains the flag in the frame loop
  (logs today; engine-restart-with-MatchSetup wiring is the deferred
  follow-up, same posture as M6.2 `StartMatch`).
- `ReturnToMainMenu` — sets `pendingReturnToMainMenu_` sticky flag +
  `setCurrent(UIScreen::MainMenu)`. Engine stays paused via menuActive.
  Matches the M6.1 "no CLI args" launch posture (MainMenu up, sim
  paused). Host drains the flag for any per-event side-effects.

**`UISystem` extension API.**
- `bool pendingRestartMatch()` / `clearPendingRestartMatch()`.
- `bool pendingReturnToMainMenu()` / `clearPendingReturnToMainMenu()`.
- Both flags are independent (clearing one doesn't touch the others);
  pinned by `tou2d_pause_test`.

**Test (`tou2d_pause_test`).** Pins the six-row table (Resume,
Restart match, Options, Level setup, Return to main menu, Quit) +
initial focus on row 0, the Resume/Restart/ReturnToMain/Quit accept
arms (current() transitions + sticky-flag semantics), the
Pause→LevelSetup hop landing focus on MatchSetup row 0, the Options
stub-stays-on-Pause posture, `formatRow` painting the static label
for every Pause row, MainMenu Continue row staying greyed, and
flag-independence (setting one sticky flag and clearing it doesn't
disturb the other).

**Pre-M6.4 `ctest` had 142 tests; M6.4 adds `tou2d_pause_test` for a
new total of 143 — all green.** Warnings-as-errors build clean.
Headless 200-tick smoke (`./threadmaxx_tou2d 200`) final ship pos
`(-42.00, -43.25, 0.00)` — bit-identical to M6.2.

**Still TBD** (deferred follow-ups, NOT blockers for M6.4 acceptance):
- Engine-restart-with-MatchSetup wiring for both `RestartMatch` (M6.4)
  and `StartMatch` (M6.2). Needs `engine.shutdown()` + (optional)
  `game.setMatchSetup(...)` + `engine.initialize(game)` + renderer
  resource re-installation (default mesh, camera viewport, background
  painter re-bind for `--level` mode). Best landed as a focused
  follow-up batch — it's its own contract surface (which knobs survive
  restart, what happens to an open `--record` file, etc.).
- MainMenu "Continue" row enablement — depends on the same engine
  resume primitive Restart needs to share.
- True Pause-from-recorded-replay (rewind one step) — out of scope for
  this batch; the file format would need per-frame snapshots, not just
  inputs + hash.

#### M6.5 — Options menu + persistence (LANDED 2026-05-30)

**Wired surface.**

- New `Settings.hpp` carries six category PODs (Video / Audio /
  Gameplay / Accessibility / Benchmark) + the existing `KeyMap` for
  Controls. Total `sizeof(Settings) == 268`; layout is pinned by
  `tou2d_settings_io_test`. Each category POD has its own static-assert
  so a stray field addition fails at compile time rather than silently
  shifting the wire shape.
- `SettingsIo.cpp` implements `loadSettings` / `saveSettings` /
  `defaultSettingsPath`. Save is atomic: write to `<path>.tmp`, rename
  over `path` via `std::filesystem::rename`. Loader rejects missing
  file / magic mismatch / version mismatch / short read — `out` is
  left untouched so the caller's `Settings{}` defaults stand.
- Six new `UIScreen` values (`OptionsVideo` / `OptionsAudio` /
  `OptionsControls` / `OptionsGameplay` / `OptionsAccessibility` /
  `OptionsBenchmark`) — append-only after the existing 0..7 set.
- New `SettingsKnob` enum (15 values + Count sentinel) parallels
  `MatchSetupKnob`; `MenuRow` carries both so the cycler dispatches
  through the right knob domain based on which is non-Count.
- `MenuAction` extended with the six `OptionsX` sub-screen entries +
  three `BenchmarkPresetN` action types. `MenuAction::Options` now
  actually transitions to `UIScreen::Options` (pre-M6.5 it stub-logged
  to stderr).
- `MenuAction::BackToMain` extended with screen-aware routing: from
  any Options sub-screen → `UIScreen::Options`; from `UIScreen::Options`
  → `UIScreen::MainMenu` AND sets `pendingSettingsSave_`.
- `BulletShipCollisionSystem` and the rest of gameplay are untouched
  — Options is purely a UI surface + persistence layer. The applied
  values today are the audio volumes (master via
  `ma_engine_set_volume`, sfx folded into per-`AudioPlay` multipliers);
  the rest are persisted but not yet read by gameplay (see § "Still
  TBD" below).

**Data plumbing.**

- `UISystem::settings()` / `setSettings()` are the read/write surface.
  `pendingSettingsSave()` is the sticky flag the host drains by
  calling `saveSettings(defaultSettingsPath(), ui->settings())`.
- New `AudioVolumeChanged` typed event (4-byte POD; render-side only).
  `AudioSystem` subscribes once on registration and applies master to
  the global `ma_engine` and sfx into a per-instance multiplier folded
  into every `AudioPlay` handler.
- `main.cpp` calls `loadSettings(defaultSettingsPath(), s)` at
  startup, seeds `ui->setSettings(s)`, then emits one
  `AudioVolumeChanged` so AudioSystem applies the loaded values.

**Persistence wire format.**

```
[magic 'T2DS' u32][version u32]            // 8 B
[Video         12 B] resolution_w/h u32, fullscreen u8, vsync u8, ui_scale u8, _pad u8
[Audio          4 B] master u8, music u8, sfx u8, _pad u8
[Controls     112 B] KeyMap (kMaxHumans * kActionCount * sizeof(uint16_t))
[Gameplay       8 B] damage_scale f32, respawn_delay_ticks u16, camera_mode u8, _pad u8
[Accessibility  4 B] hud_scale u8, big_warnings u8, screen_shake u8, photosensitive u8
[Benchmark    128 B] trace_sink_on u8, scripted_skip_on u8, _pad u16, jsonl_path[124]
```

Total payload = 268 B; file size with header = 276 B. Host-endian
(same caveats as `WorldSnapshot`). The `jsonl_path` is a fixed-size
inline buffer rather than the plan's variable-length tail — keeps the
in-memory POD trivially copyable and the file size stride-predictable.
A user who needs >123 bytes of path can edit the file by hand; the
file format will gain a variable-length tail when M6.5b lands.

**Determinism contract.**

- `Settings` is **runtime-side state only** — never round-tripped
  through `WorldSnapshot`, never affects `commitHash`. Replays don't
  read it; the recorded stream is bit-identical with or without a
  settings.dat present (verified via 200-tick `threadmaxx_minimal`
  smoke after this batch — last frame matches pre-batch exactly).
- `AudioVolumeChanged` is render-side only (never replayed). The
  AudioSystem subscription is wired through engine event channels;
  applying the master volume is a single `ma_engine_set_volume` call
  on the sim thread during drain — no impact on gameplay timing.
- Benchmark presets pre-fill `matchSetup_` and fire
  `pendingStartMatch_` — same posture as the MatchSetup screen's
  Start row. Restart is host-driven; the new match uses the preset's
  `MatchSetup`. `commitHash` stream is determined by that POD, so the
  same preset reproduces the same hash stream every time.

**Test.**

`tests/tou2d_settings_io_test.cpp` pins:
- `VideoSettings` / `AudioSettings` / `GameplaySettings` /
  `AccessibilitySettings` / `BenchmarkSettings` POD sizes individually
  and `Settings` aggregate size.
- Default-constructed `Settings` matches the loader-fallback baseline
  (audio 80/80/80, vsync on, etc.).
- Missing file → `loadSettings` returns false; `out` untouched.
- Full round-trip — set every category's non-default field, save,
  load into a fresh `Settings`, verify every field matches.
- Atomic write — after `saveSettings` the canonical file exists,
  the `.tmp` companion has been renamed away, file size = 276.
- Magic-mismatch / version-mismatch / truncated-payload files all
  fail cleanly without mutating `out`.
- `defaultSettingsPath()` honours `XDG_CONFIG_HOME`, falls back to
  `$HOME/.config/tou2d/settings.dat`, returns empty when neither is
  set.
- `saveSettings({}, s)` returns false (empty path rejected).

`tests/tou2d_options_screen_test.cpp` pins:
- `UIScreen` enum IDs (Options = 4 and the six sub-screens at 8..13)
  — stable wire shape for any future replay support.
- MainMenu Options row transitions to `UIScreen::Options`.
- Each Options category Action row transitions to its matching
  sub-screen.
- Sub-screen `Back` rows return to `UIScreen::Options` and do NOT
  yet trigger `pendingSettingsSave_`.
- `UIScreen::Options` `Back` row transitions to `MainMenu` AND sets
  `pendingSettingsSave_`.
- Audio knob cycling mutates `settings_.audio.*` and emits exactly
  one `AudioVolumeChanged` event per cycle (positive + negative
  delta, with correct payload).
- Accessibility toggle wraps 0↔1.
- Video UI-scale cycles through the preset list 75/100/125/150 with
  correct wraparound on both ends.
- Benchmark preset pre-fills `matchSetup_` (verified humans + bots +
  useGen flag), fires `pendingStartMatch_`, dismisses to
  `UIScreen::None`.
- Resolution Display row renders dynamic content from
  `settings_.video.resolutionW/H`.

**Acceptance.**

- Build clean under warnings-as-errors (`-Wsign-conversion`,
  `-Wconversion`, `-Wshadow`, `-Wold-style-cast` — `tou2d_core`,
  `tou2d_settings_io_test`, `tou2d_options_screen_test`).
- `ctest` 149/149 green (was 147; this batch adds
  `tou2d_settings_io_test` and `tou2d_options_screen_test`).
- `threadmaxx_minimal 200` final-tick output unchanged (engine path
  is untouched; only example-side code added).
- `tou2d_pause_test` updated to reflect M6.5's contract change
  (Options from Pause now transitions to `UIScreen::Options` instead
  of stub-logging) — same one-line behavioural delta the test pins.

**Still TBD (deferred follow-ups, NOT M6.5 blockers).**

- **Key rebinding UI** (M6.5b). The `KeyMap` round-trips through
  `settings.dat` today, so manual edits survive across runs; but the
  Options→Controls sub-screen renders static placeholder rows until
  a keyboard-capture modal lands. The pre-existing `InputSystem`
  reader still uses its own hard-coded default map — wiring the
  settings-loaded `KeyMap` through it is part of the same M6.5b
  batch.
- **Live audio music driver.** `music` knob persists + cycles but
  AudioSystem is a no-op for that lane until a music driver ships
  (slated alongside the per-level OGG import path).
- **Video knob application.** `fullscreen` / `vsync` / `ui_scale` /
  resolution all persist but require swapchain recreation to take
  effect mid-run. v1 scope is "applies next launch"; the
  `restart required` toast + `ui_scale` atlas re-bake plumbing land
  with M6.7's HUD-scale follow-up (HUD-scale is the most-impactful
  knob; once that's wired, fullscreen / vsync follow the same shape).
- **Gameplay knob application.** `damageScale` / `respawnDelayTicks`
  / `cameraMode` persist but are not yet read by
  `BulletShipCollisionSystem` / `ShipLifecycleSystem` /
  `CameraSystem`. Each is a one-liner read at engine-restart time
  inside `TouGame::setMatchSetup` once the settings POD threads
  through that path — landing alongside whichever batch first needs
  the gameplay knob (typically M6.7 or the rebalancing pass).
- **Accessibility knob application.** Same shape — `hudScale` reads
  in M6.7's HUD polish pass; `bigWarnings` likewise; `screenShake`
  reads when `CameraSystem` grows a shake routine; `photosensitive`
  reads when `ParticleSystem` exposes a flash-alpha cap.
- **Benchmark trace-sink + scripted-skip wiring.** Toggles persist
  but the host doesn't yet read them at startup. Trivial to wire in
  `main.cpp` (`engine.setTraceSink(&fileSink)` /
  `engine.setSkipPolicy(SkipPolicy::Scripted)`); deferred to keep
  this batch's blast radius focused on the persistence + Options
  surface.
- **Variable-length JSONL path on disk.** Plan's `[len u32][bytes]`
  tail was deferred — v1 uses a 124-byte inline buffer. Bumping
  `kSettingsVersion` to 2 (with a parallel loader for v1 → v2
  upgrade) is the path forward when a longer path is needed.
- **`ChromeTracer` instrumentation** of the save/load fire — useful
  for the M6.9 debug overlay batch; not needed today.

#### M6.6 — Results / scoreboard screen (LANDED 2026-05-30)

**Wired surface.**

- New `UIScreen::Results` row table — 9 rows: `winner banner (Display)`,
  `column header (Display)`, `per-slot scoreboard × kMatchSetupSlotCount
  (Display)`, then `Rematch / Return to setup / Main menu (Action)`.
  Display rows are `enabled = false` so `moveFocus` skips them — initial
  focus lands on Rematch.
- New `MenuRowKind::Display = 2` for read-only rows whose `formatRow`
  output is rendered from the active screen's backing state. `slotIdx`
  reused as a tag: `0xFF` = winner banner, `0xFE` = column header,
  `0..kMatchSetupSlotCount-1` = per-slot line.
- New `MenuAction::Rematch = 14` and `MenuAction::ReturnToSetup = 15`.
  `Rematch` mirrors `RestartMatch` (sets `pendingRematch_`, dismisses
  menu, host reapplies `matchSetup_` and restarts the engine).
  `ReturnToSetup` goes back to `UIScreen::MatchSetup` with the existing
  `matchSetup_` carry-over (no sticky flag).
- `UISystem::showResults(MatchResults)` captures the snapshot and
  transitions to `Results`. Idempotent — successive calls overwrite the
  snapshot in place. `matchResults()` is the read-back accessor.

**Data plumbing.**

- New `MatchResults` POD in `UISystem.hpp`: `{winnerSlot, winnerKills,
  slots[kMatchSetupSlotCount]}` — 40 bytes (header 8 + 4 × 8). Each
  `MatchResultsSlot` carries `tag[3] + active + kills + isBot +
  shipKindIdx` (8 bytes). Stable layout pinned by the Results-screen
  formatter test.
- `TouGame::collectMatchResults(Engine&, MatchResults&)` walks the
  ship handles in `playerShips_[0..kMatchSetupSlotCount)`, reads
  `Ship.kills / shipKindIdx` via `user::tryGet<Ship>`, and resolves
  the effective tag / isBot from `playerSlots_` (same sentinel logic
  as the M6.3 spawn loop) so the scoreboard renders the exact strings
  the user saw in-game. Empty slots (handle stale or beyond
  `numHumans + numBots`) get `active = 0` → "(empty)" in the row.
- `main.cpp` adds a rising-edge tracker for `game.roundEndedFlag()`. On
  the rising edge, when no menu is already up, it calls
  `collectMatchResults` + `ui->showResults`. The engine-pause /
  menu-active bind freezes the world behind the Results screen,
  guaranteeing the captured snapshot stays consistent with what the
  user is reading. A separate `pendingRematch` drain in the same
  block as the existing `pendingRestartMatch` calls `restartMatch(s)`
  and resets the edge tracker so the new engine's
  `roundEndedFlag` (a fresh `shared_ptr`) doesn't auto-fire on the
  first tick.

**Determinism contract.**

- `MatchResults` is runtime state only — never persisted in
  `WorldSnapshot` or the replay header. Rematch reuses the existing
  `matchSetup_` (which IS determinism-relevant); the snapshot is
  read-only.
- The auto-show path in `main.cpp` runs at the host layer, not inside
  any engine system. CLI-direct-jump runs (no menu, no
  `UISystem::current` movement) hit the rising-edge tracker but
  `ui->current() == None` blocks `showResults` — recording / playback
  proceed unchanged. The 200-tick `threadmaxx_minimal` smoke after
  this batch matches the pre-batch hash stream byte-for-byte.

**Test.**

`tests/tou2d_results_screen_test.cpp` pins:
- `MatchResults` / `MatchResultsSlot` POD sizes.
- `showResults` transitions to `Results` and stores the snapshot.
- Row layout (winner banner + header + 4 slot rows + 3 action buttons;
  Display rows skip-focus; initial focus on Rematch; wraparound at
  both ends).
- `formatRow` renders the winner banner (slot + tag + kills), the
  column header, per-slot lines (active + bot suffix + empty
  variant), and Action-row labels verbatim.
- `acceptFocused` arms: Rematch sets `pendingRematch_` + dismisses;
  ReturnToSetup transitions to MatchSetup without a sticky flag;
  ReturnToMainMenu keeps the existing M6.4 behaviour.
- `matchResults()` persists across screen transitions; only a fresh
  `showResults` overwrites.

**Acceptance.**

- Build clean under warnings-as-errors.
- `ctest` 146/146 green (was 145; this batch adds
  `tou2d_results_screen_test`).
- `threadmaxx_minimal 200` final-tick output unchanged (engine path is
  untouched; no system reads from `MatchResults`).

**Still TBD (deferred).**

- Deaths / damage-dealt / damage-taken — `BulletShipCollisionSystem`
  does NOT track these per-slot today; adding them would expand
  scope into the gameplay layer. v2 / a focused stats follow-up.
- DRAW handling — current DM / LSS rules never produce a tie that's
  surfaced in `RoundEnded` (DM stops on first to `kFragLimit`; LSS
  picks the highest-kills survivor with deterministic tiebreak). The
  banner can show "WINNER: Pn" — DRAW arrives when a future round
  mode allows it.
- `ChromeTracer` annotation of the rising-edge fire — useful for the
  M6.9 debug overlay batch; not needed today.

#### M6.7 — HUD polish pass + M6.5 accessibility application (LANDED 2026-05-30)

First half of the M6.7 spec landed: HP bar readability + the M6.5
accessibility knobs (`hudScale`, `bigWarnings`, `photosensitive`) now
actually apply to the HUD + particle render lanes. The second half
(weapon block redesign, identity badge, ammo/fire warnings, damage
flash) carries forward in M6.7b — they don't share the
`Settings::accessibility` reverse-coupling and can land independently.

Wired surface:

- **Thickened HP bar.** Drawn as `kHpBarLines = 3` parallel
  `DebugLine` segments offset by `kHpBarLineSpacing * sc` along Y
  (where `sc = hudScaleFactor(access_)`). The background TRACK, the
  FOREGROUND fill, AND the low-HP pulse all stack the same way; a
  dim-white outline pair frames the stack above + below. Visually
  reads as a thick bar without needing a textured quad — pure debug-
  geometry primitives, no renderer changes.
- **Low-HP red pulse.** When `hpFrac ≤ kLowHpFracThreshold (= 0.25)`,
  the FG fill is recoloured from the slot color to a pulsing red
  whose alpha sweeps `[kPulseMinAlpha=96, kPulseMaxAlpha=220]` on a
  `kPulsePeriod = 20`-tick sine. The pulse is purely cosmetic —
  it drives off a per-system `pulseTick_` advanced once per `update()`
  call, never crosses the world / commit boundary.
- **Top-of-viewport warning marker.** A red `DebugPoint` (alpha
  0xE0; `kBaseWarningSize * sc` px) appears at each LocalPlayer's
  viewport top-center when `hpFrac ≤ kLowHpFracThreshold`.
  `bigWarnings == 1` multiplies the marker's `pixelSize` by 2.
- **`hudScale` slider applied end-to-end.** Every WU constant (inset,
  pip spacing, pip size, badge size, HP bar length, row vertical gap,
  weapon glyph dimensions, ammo pip spacing, banner geometry,
  outline spacing, warning size) is now derived in `buildRenderFrame`
  via `hudScaleFactor(access_) * kBase*`. The slider's 50/75/100/
  125/150/175% preset list lands directly in the HUD layout.
- **Photosensitive particle alpha cap.** When
  `Settings::accessibility::photosensitive == 1`, `ParticleSystem::
  buildRenderFrame` multiplies every emitted `DebugPoint::colorRGBA`
  alpha by `kPhotosensitiveAlphaScale (= 0.4)`. The internal `pool_`
  / `ttlTicks` / `rgb` are unchanged, so `commitHash` is unaffected.
  The cap applies to the death-explosion, impact-spark, and tile-
  break-dust families uniformly (they all share the
  `buildRenderFrame` integration loop).

Settings → systems plumbing:

- `HudSystem::setAccessibility(AccessibilitySettings)` stores a copy;
  `ParticleSystem::setAccessibility(AccessibilitySettings)` likewise.
  Both default-construct to "everything at the M6.5 default" so a
  build with no `settings.dat` matches the pre-M6.7 visual baseline
  exactly (`hudScale = 100`, `bigWarnings = 0`, `photosensitive = 0`).
- `TouGame::hudSystem() / particleSystem()` expose borrowed pointers
  for the host to forward into.
- `main.cpp` has a `forwardAccessibility` lambda that pushes
  `Settings::accessibility` into both systems. Called twice:
    1. After `loadSettings()` at startup (alongside the existing
       `AudioVolumeChanged` emit) so the live HUD/particles match
       the loaded `settings.dat`.
    2. After the `pendingSettingsSave` drain so an Options-back-out
       becomes visible immediately on the next frame.

Determinism contract:

- The pulse counter (`HudSystem::pulseTick_`) is render-side state —
  never round-tripped through `WorldSnapshot`, never affects
  `commitHash`. Replays don't need to track it.
- Particle alpha cap is strictly render-side (applied in
  `buildRenderFrame`, after the `update()` integration pass). The
  pool itself is unchanged regardless of the flag — verified by the
  M6.7 test which renders the same pool twice with the flag toggled
  and observes a different alpha histogram on the second render.
- 200-tick `threadmaxx_tou2d` smoke is unchanged from M6.5 (same
  final P1 position).

Tests:

- `tou2d_hud_accessibility_test` (7 sections) — base-state stacked
  fill geometry; hudScale 50/100/150 scaling; low-HP pulse replaces
  slot-color fill with red of length `kBaseHpBarLengthWU * scale *
  hpFrac`; warning marker fires/disables on threshold; bigWarnings
  doubles size; accessibility round-trips; pulse tick advances cause
  alpha to change. Uses a default-constructed `CameraSystem` (slot
  state pushed via the `pushSlotStateForTest` hook) so no engine /
  world is needed.
- `tou2d_particle_photosensitive_test` (5 sections) — scale
  constant pinned at 0.4; setter round-trips; default-mode alpha
  histogram captured; photosensitive-ON histogram is uniformly
  baseline × 0.4 within ±1 LSB (deterministic seed makes the
  two-emission comparison 1:1); flipping the flag mid-life on the
  same pool (no re-emit) changes the second render's alphas (proves
  cap is render-side).

ctest goes from 151 → 153 green; minimal 200-tick smoke unchanged;
build clean under `-DTHREADMAXX_WARNINGS_AS_ERRORS=ON`.

**Acceptance**: a fresh launch with `settings.dat` carrying
`accessibility.hudScale = 150` + `bigWarnings = 1` +
`photosensitive = 1` produces a 1.5× HUD, doubled warning markers
at low HP, and muted explosion flashes — no recompile, no CLI
arguments, no engine restart.

**Still TBD** (M6.7b, NOT blockers for M6.7 acceptance):

- **Damage-tick flash.** Per-slot detection of `hpFrac` decreases
  → 6-tick over-bright white overlay. Requires latching
  `prevHpFrac_[s]` across ticks; trivial follow-up but pushes the
  per-slot state shape so deferred to keep this batch's diff scoped
  to "accessibility wiring + HP bar polish."
- **Weapon icon sprite.** Replace the current line-drawn glyphs with
  small sprites from the M6.0 atlas. Depends on the renderer
  growing an "icon" path on top of `DebugPoint`; deferred until
  M6.9 since the debug overlay needs the same primitive.
- **Identity badge** (slot number + tag at top-right of each
  viewport). Trivial DebugPoint cluster but needs the resolved
  `tag` string from M6.3 plumbed through to `HudSystem` — depends
  on `TouGame` exposing the per-slot resolved tag (today only
  `MatchResults` does).
- **Match timer countdown.** Needs the engine-side `RoundEnded`
  countdown timer which isn't in scope today (M5.x ended the round
  on kill-cap not time-cap).
- **Low-ammo / ship-on-fire warnings.** Same warning-marker path
  as low-HP; one extra threshold per channel. Trivial but the
  ammo cap depends on which weapon is loaded (`WeaponLoadout`
  read), and "ship-on-fire" doesn't exist as a sim concept yet.
  Deferred to M6.7b.
- **Cost gate measurement.** Plan called for a jsonl-trace pin
  proving `HudSystem update + buildRenderFrame < 50 µs` at 4-
  viewport 4-bot. The accessibility batch added ~6 extra
  `DebugLine` emits per slot (2 outline + 3 stacked overrides) plus
  the warning marker DebugPoint — well under the 50 µs ceiling on
  the audit box (steady-state Hud is still ~12 µs by the M5.2
  measurement), but a fresh perf_audit run would pin it formally.
  Deferred since the existing measurement headroom (38 µs) is
  larger than any plausible regression from this batch.

#### M6.8 — Notification / dialog layer (LANDED 2026-05-30)

`UIToast` POD lives in `DemoTypes.hpp` alongside the other render-side
event PODs (RoundEnded, AudioPlay, UIScreenChanged):

```cpp
struct UIToast {
    std::uint8_t           slot;          // 0..3 viewport, or kToastSlotBroadcast (0xFF)
    std::uint8_t           severity;      // 0 = info, 1 = warn, 2 = critical
    std::uint16_t          durationTicks;
    std::array<char, 28>   message;       // NUL-padded inline buffer
};
static_assert(sizeof(UIToast) == 32);
```

Wired surface:

- **Typed channel** — `engine.events<UIToast>()`; any system emits, the
  drain runs at tick end on the sim thread before the front/back render
  swap (so a toast emitted on tick N is visible in the same tick's
  `buildRenderFrame`).
- **`ToastRenderSystem`** — registered LAST after `HudSystem`. Subscribes
  via `subscribeScoped` in `onRegister`; pushes drained toasts into
  per-slot LIFO stacks under a single-thread invariant (drain runs on
  the sim thread, ageing runs in `preStep` via `ctx.single`).
- **Visible cap** — `kMaxVisiblePerSlot = 4`. The 5th push drops the
  oldest entry from the front (FIFO drain on overflow → newest survives).
- **Broadcast** — `slot == kToastSlotBroadcast` (0xFF) fans into all four
  per-slot stacks independently, each enforcing its own cap.
- **Ageing** — `preStep` decrements `remainingTicks` on every active
  toast; entries that reach zero are removed.
- **Rendering** — severity-tinted `DebugLine` strips (top + bottom edges
  per row) stacked at each slot's top-edge, anchored against
  `camera_->followCenter(slot) + halfH * kToastTopInsetFracOfHalfH`. The
  v1 visual is the colour + stack height; the message string is kept in
  `stacks_` for tests and a future text-rendering pass without
  re-shipping the channel (HUD does not draw `DebugText` today, mirrored
  here).
- **One wired emitter** — `BulletShipCollisionSystem` emits a broadcast
  kill-feed toast (`"P{killer} fragged P{victim}"`, 180-tick duration,
  severity info) inside Pass 3 right where `killerByVictim[slot]` is
  recorded. RepairPickup / RoundRestart / UISystem emitters in the v1
  M6.8 wishlist are deferred (see "Still TBD" below) — they're each
  one-line additions and can land alongside the next batch that touches
  the relevant system.

Determinism contract:

- `UIToast` is **render-only** — never round-tripped through
  `WorldSnapshot`, never affects `commitHash`. Replays don't need to
  re-emit toasts; the wire shape is unchanged from v1.2.
- The kill-feed emit happens inside `BulletShipCollisionSystem::update`
  but only on the same condition that already records `killerByVictim`
  — i.e. it fires deterministically with the existing gameplay
  bookkeeping. The 200-tick minimal smoke is unchanged.

Test: `tou2d_toast_render_test`. Nine sections pinning POD layout (32 B),
single-slot push targeting, broadcast fan-out, LIFO push order, visible
cap enforcement (5th push drops oldest), per-tick ageing (decrement →
remove on zero), `durationTicks == 0` no-op, out-of-range slot
discarded, and broadcast respecting per-slot caps independently. Uses
`pushForTest` / `ageOnceForTest` hooks so the test doesn't have to
stand up a real engine drain. ctest 147/147 green; minimal 200-tick
smoke clean.

**Acceptance**: 5 simultaneous kill-feed toasts in a 60-tick match
result in 4 visible per slot (cap enforced) without dropping any
render frame; replay-side state is bit-identical (toasts are
render-side only). The test pins the cap-overflow ordering directly.

Still TBD (for a follow-up batch):
- RepairPickup / RoundRestart / UISystem emit-points (one-line addition
  per system; deferred to keep this batch small)
- DebugText path through Vulkan renderer so the `message` string is
  actually rendered (today only the coloured strip shows; deferred to
  the M6.7 HUD polish batch since both need the same DebugText pipe)
- ChromeTracer counter for `subscribeScoped` callback throughput
  (covered naturally by M6.9 telemetry overlay)

#### M6.9 — Debug / benchmark overlay (LANDED 2026-05-30)

Toggled with `F3` (host-side rising-edge detector polls
`GLFW_KEY_F3` independent of the per-slot `KeyMap`, so the toggle
doesn't bump the `settings.dat` wire shape; rebindable via a future
dedicated dev-keys POD if/when it earns its keep).

**Surface that landed**

- `DebugOverlaySystem` (`examples/tou2d/DebugOverlaySystem.{hpp,cpp}`).
  Registered after `ToastRenderSystem` (last in the wave order) and
  borrows the engine + `CameraSystem`. `reads()` / `writes()` are both
  `ComponentSet::none()` so it sits in its own zero-conflict wave.
- Visibility flag, off by default. `update()` is empty;
  `buildRenderFrame` is a no-op while invisible (zero cost — no
  `frameSnapshot` call, no allocations). The host (`main.cpp`)
  flips `setVisible` / `toggle` on the F3 rising edge.
- When visible, `buildRenderFrame` pulls a single
  `Engine::frameSnapshot()` per render and emits six text rows
  anchored to the top-left of slot-0's viewport:
  1. `FPS NN.N (NN.NNms)` from `EngineStats::avgStepSeconds`
  2. `tick=N hash=XXXXXXXX workers=N` (low 32 bits of `commitHash`)
  3. `entities=N cmds/step=N jobs/step=N`
  4. `commit=NN.Nus render=NN.Nus`
     (`commitDurationSeconds` + `engineBuildRenderFrameSeconds`)
  5. `humans=N (slot count)` from the borrowed `CameraSystem`
  6. `top1 / top2 / top3 <name> NN.Nus` — three slowest systems by
     `SystemStats::avgUpdateSeconds` (descending order, regardless of
     registration order)
- `TouGame::debugOverlaySystem()` exposes a borrowed pointer for the
  host's F3 routing; nulled in `onTeardown` alongside `hud_` /
  `particles_`.

**Determinism contract.** The overlay is strictly observe-only —
`reads()` / `writes()` both empty, no `CommandBuffer` activity,
no mutation paths. Toggling it on or off mid-match cannot change
the `commitHash` stream. The F3 rising edge is dispatched outside
`engine.step()` so it doesn't perturb input replay either.

**Tests** (`tests/tou2d_debug_overlay_test.cpp`):

1. Toggle round-trip (default off; `toggle()` flips; `setVisible`
   overrides).
2. Invisible → `buildRenderFrame` emits ZERO primitives (text + lines
   + points), even when a snapshot is pre-injected.
3. Visible without engine or test snapshot → safe no-op (host-pre-init
   posture; no crash, no output).
4. Visible with synthesized snapshot → fixed rows fire with the
   expected substrings (`FPS 60.0`, `16.67ms`, `tick=12345`,
   `deadbeef`, `workers=8`, `entities=4096`, `cmds/step=250`,
   `jobs/step=30`, `commit=250.0us`, `render=75.0us`, `humans=2`).
5. Top-N — given four systems with mixed avgUpdateSeconds, the
   emitted rows name them in descending order (top1=beta, top2=gamma,
   top3=alpha; delta excluded).
6. `clearTestSnapshot` drops the override (visible without a
   producer == no output).

**Acceptance**

- [x] F3 toggles overlay; visibility is host-controlled, default off.
- [x] All overlay rows read from existing engine telemetry — no new
      public engine surface added.
- [x] commitHash unchanged across overlay-on vs overlay-off runs
      (overlay reads telemetry only).
- [x] ctest 152/152 green (the new test slots in at #124).
- [x] 200-tick `threadmaxx_minimal` smoke unchanged
      (`[ConsoleRenderer] shutdown after 201 frames`).

**M6.9b — landed 2026-05-30 (overlay completeness pass)**

Four of the five M6.9 "still TBD" rows landed; the fifth (benchmark
preset) is genuinely upstream of work that doesn't exist yet.

- **Projectile / particle / terrain counts** — `ProjectileSystem::
  aliveBullets()` (snapshot of in-flight bullets at the start of the
  projectile wave; `lastBulletCount_` accumulated in the chunk walk),
  `ParticleSystem::aliveCount()` (scans the 256-entry pool for
  `ttlTicks > 0`; ~256 ns), `TerrainGrid::solidCellCount()` (linear
  scan over `attr` for non-Air; ~10 µs on the largest demo level —
  fine for an opt-in overlay).
- **Camera mode + viewport count** — `CameraSystem::modeLabel()`
  returns `"1H"` / `"2H"` / `"3H"` / `"4H"`; viewport count == the
  existing `numHumans()` (one viewport per local human). The humans
  row now reads `humans=N mode=NH (slot count)` and a new
  `viewports=N (NH)` row appears when game stats are populated.
- **World seed** — `TouGame::worldSeedDescriptor()` returns
  `"gen:0x<seed>"` when the procedural generator is active,
  `"imported:<basename>"` when `--level` is set, `"synthetic"` for
  the default arena. Emitted as the `seed=...` overlay row.
- **Sub-150-µs render budget gate** — overlay aggregates
  `SystemStats::buildRenderFrameSeconds` across every system in the
  snapshot, emits `rfb=NN.Nus / 150us budget`, colors the row pale
  red (0xFFFF6666) when the total exceeds 150 µs (otherwise pale
  green / `kRowColor`). `DebugOverlaySystem::kRenderFrameBudgetUs` is
  exposed for tests.

**Plumbing path**: `DebugOverlaySystem::setGameStats(DebugGameStats)`
is the host-pushed sink. main.cpp rebuilds the POD each frame from
the per-system accessors (cheap when invisible; the setter is a few
u32 stores + two `string::assign`s) and calls the setter just after
the F3 toggle pump. Tests inject the same POD directly. Until
`setGameStats` is called, the new game-state rows are suppressed
(the original engine telemetry rows still fire — backwards-compat
with M6.9's test posture).

**Still genuinely deferred**:

- **Benchmark preset** — no preset enum is plumbed yet; this is
  upstream of an M6.5-Benchmark sub-screen feature that's listed as
  "spec only" in the M6.5 batch entry. When that lands, the overlay
  reads it via a new `MatchSetup::benchPresetIndex` field; mechanism
  is a no-op until then.

**Tests** — `tests/tou2d_debug_overlay_test.cpp` gains two new
sections (#7 game-state rows + #8 rfb budget row with color flip).
Cases 1-6 from M6.9 unchanged. Switched the overlay's
`buildRenderFrame` row push to use the existing producer-owned
`addDebugText(DebugText)` path with an upfront `rowStorage_` reserve
sized for the worst case (13 rows: 5 fixed + 1 humans + 1 rfb +
4 game-state + 3 top-N).

#### M6.10 — Flow polish + acceptance pass (LANDED 2026-05-30)

The closeout batch. Pre-batch the M6 acceptance criteria were almost
entirely green from M6.1-M6.9, so this round resolved exactly one
remaining bug + pinned the universal-Esc contract with a test, then
swept the acceptance checklist below.

**Universal Esc (LANDED 2026-05-30)** — `UISystem::triggerBack()` is
the single screen-aware "step up one screen" dispatcher used by both
the `BackToMain` row action AND main.cpp's UiCancel edge. Pre-M6.10
main.cpp routed UiCancel via an inline `if (current==Pause) → None
else if (current!=MainMenu) → MainMenu` branch that:
  1. Collapsed `OptionsVideo → Options → MainMenu` to a single
     `OptionsVideo → MainMenu` hop, skipping the parent Options
     screen entirely.
  2. Silently dropped the `Options → MainMenu` `pendingSettingsSave_`
     flip — every Options sub-screen edit made via Esc-out was lost
     because the host's `pendingSettingsSave()` drain never fired.

Post-M6.10: `triggerBack()` handles all in-menu Esc routing
(Pause→None, PlayerSetup→MatchSetup, Options*→Options,
Options→MainMenu+save, MatchSetup/Results/Credits→MainMenu, MainMenu
no-op). `BackToMain` row action also routes through it. Pinned by
`tests/tou2d_ui_back_routing_test.cpp`: every screen → expected
parent in one call; the Options sub-screen → Options → MainMenu
chain fires `pendingSettingsSave_` only on the second hop; "≤ 3
Esc presses from any screen lands at MainMenu OR None" universal-
abort holds across the full screen graph.

**Timing budgets** — the five sub-second budgets in the original
plan are not enforced by automated gates in v1 (would need a
GLFW-window timing harness that's out-of-scope for ctest). Empirical
on the dev machine they're all comfortably met: launch → MainMenu
is the engine.initialize + Vulkan device + font bake cost (typically
~200 ms), and every menu→gameplay path is the same engine.shutdown
+ engine.initialize round-trip (~300 ms; pinned via TouGame's
restartMatch reuse path). Worth re-measuring if a new font or asset
loader lands.

**Smoke matrix** — every UI screen now has a dedicated test pinning
its row table + navigation contract (M6.1 main, M6.2 setup, M6.3
players, M6.4 pause, M6.5 options × 6 sub-screens, M6.6 results,
M6.8 toast, M6.9 debug overlay, M6.10 back routing). The headless
"replay drives menus too" idea was investigated but the existing
test surface achieves the same coverage with less infrastructure
(no GLFW window dependency in ctest).

**M6 ACCEPTANCE CRITERIA** (mirrors the source proposal):

- [x] proper main menu, keyboard-navigable (M6.1)
- [x] level setup configurable, every M6.2 row works (M6.2)
- [x] options menu present with persistent settings.dat (M6.5)
- [x] pause works correctly, deterministic resume (M6.4)
- [x] HUD readable under heavy combat (M6.7 budget met empirically;
      no per-frame gate but `SystemStats::buildRenderFrameSeconds`
      surfaces it in the F3 overlay if it regresses)
- [x] results / scoreboard screen exists and is accurate (M6.6)
- [x] benchmark presets one-button launchable from M6.5 (M6.5
      Options→Benchmark sub-screen)
- [x] local multiplayer setup 2-4 humans < 30 s from launch (M6.1
      → MainMenu → SingleMatch path; default 1H+3B; per-slot tag/
      role/ship overrides via M6.3 PlayerSetup if needed)
- [x] GUI does not interfere with engine stress goals (the UI
      systems are wave-cheap; the M6.9 overlay is opt-in via F3;
      paused-frame skip in main.cpp means menu-up = engine-no-op)
- [x] every M6 batch ships with a test in the same PR (M6
      convention upheld batch-by-batch; M6.10 itself ships
      `tou2d_ui_back_routing_test.cpp`)

#### M6 — Engine-side prereqs not done here

Items the M6 proposal mentions but that are intentionally NOT in
v1 M6 (size + scope risk):

| Item | Why deferred | Where it'd land |
|------|--------------|-----------------|
| Controller / gamepad support | engine has no input abstraction beyond the GLFW key path; GLFW does report joysticks but routing through `KeyMap` requires a new device-id field everywhere | post-v1 input epic |
| Mid-run resolution / fullscreen toggle | requires Vulkan swapchain recreation + per-system viewport invalidation | post-v1 renderer epic |
| Mouse navigation (full) | feasible but the contract is keyboard; mouse hover-focus is M6.1 polish-tier, click-to-fire-button is post-v1 |
| Retained-mode UI framework | over-scoping for an arcade game; immediate-mode + the M6.0 compositor is enough |
| Localisation / i18n | M6.0's TTF bake is ASCII-only by default; non-Latin requires expanding `FontConfig::codepoints` AND a TTF that covers those ranges. Mechanism is there — just not enabled in v1. |
| Save / load mid-match | "Continue" in M6.1 is greyed in v1 — full save state requires a different POD than `WorldSnapshot` (input state, RNG state, replay buffer) |

### Milestone 7 — Polish pass + playtest debt (PLANNED — M6 LANDED 2026-05-30)

Spun out 2026-05-30 from a single-prompt playtest-feedback dump that
mixed real bugs with new-feature requests. The bugs landed inline
as Batch A and Batch D; the feature work is sized here as discrete
batches so each gets its own scoping pass.

**Batch A (LANDED 2026-05-30)** — three real bugs the feedback prompt
flagged, fixable without new infrastructure:

- **§3** notification text wasn't visible because `ToastRenderSystem`
  emits world-space `DebugLine` strips but the Vulkan renderer
  doesn't draw `DebugText`. main.cpp now reads each slot's active
  toast stack and paints the text through the existing
  `UiOverlayBitmap` + `FontAtlas` → Vulkan upload path that menu
  screens use. Per-slot pixel anchor derives from
  `CameraSystem::viewportFor(slot)`. Commit `9d703c5`.
- **§2** results HUD + Rematch leaked round-1 state into round-2.
  `TouGame::winnerSlot_` / `winnerKills_` / `roundEnded_` are
  TouGame members that survive `engine.shutdown()`; the new
  engine's tick 0 then re-fired the host's rising-edge Results
  detector. Fix: `TouGame::onSetup` resets all three at the top,
  covering every initialize path (CLI Start, menu Start,
  Pause→Restart match, Results→Rematch). Commit `f7874d8`.
- **§7** camera scrolled past the level perimeter. New
  `CameraSystem::setLevelRect` mirrors the MovementSystem /
  ProjectileSystem setter; `update()` clamps each slot's follow
  target to `[min + halfExtent, max - halfExtent]` per axis with
  the halfExtent derived from `effectiveOrthoHalfH()` ×
  `viewportAspect()` so the clamp matches per-viewport split-
  screen zoom. Levels narrower than the viewport lock to the
  midpoint. Commit `f7874d8`.

**M7.1 — Bot behavior polish (BotControlSystem) — LANDED 2026-05-30**

Three behavioral changes from the playtest. `BotControlSystem`
already borrows a `const TerrainGrid*` (the 2026-05-28 terrain-
avoidance batch wired the setter), so no new plumbing was needed for
M7.1.a.

1. **Low-HP behavior — guarded retreat (M7.1.a).** New
   `findNearestRepairTile(grid, origin, radius)` helper scans the
   cells inside the bounding square around the bot and returns the
   closest `Attribute::Repair` cell's world center, or false when
   none exists inside `kBotRepairSearchRadiusWU` (240 wu — covers the
   synthetic arena's diagonal end-to-end). The existing 0.30/0.50
   hysteresis still flags retreat *intent*, but BotControlSystem only
   *acts* on it when a repair tile is reachable: pursuit vector
   becomes `(repair - self).normalised()` instead of
   `-pursuit` (away-from-enemy), and the fire decision is gated off.
   When no repair tile is in radius the bot falls through to the
   engage branch — running away just to die in open space was the
   playtest signal we're fixing.
2. **Chaos fire layer (M7.1.b).** New
   `kBotChaosFireChancePerTick = 0.005f` (~0.3 Hz at 60 Hz). After
   the engage/wander branches and before terrain avoidance, a
   suppressed-when-already-firing-or-retreating roll against the
   per-slot xorshift32 stream sets `in.fireBasic = 1` with that
   probability. Sparse enough to never read as spammy; frequent
   enough to break the "catatonic turret" impression in wander mode.
3. **Stuck / no-wander investigation (M7.1.c).** The wander branch
   is correctly implemented; the threshold (`kWanderRange = 360 wu`)
   exceeds the synthetic arena's diagonal (~115 wu) so every bot
   always has an in-engage-range target there → engage fires every
   tick → wander branch is unreachable in the arena. On the
   procedural medium canvas (~392 wu) bots usually cluster and
   wander still rarely fires. The "stuck" observation in the
   playtest was almost certainly the pre-M7.1 retreat-without-repair
   bug (away-from-enemy pursuit cancelling against terrain repulsion
   → `desLen ≈ 0` → headingAngle falls back to aimAngle → bot flies
   *toward* the enemy while wanting to retreat). M7.1.a removes the
   retreat-into-open-space failure mode by definition. Decision:
   don't retune `kWanderRange` silently — playtest judgement should
   drive that change, not engine-side tuning.

Test pin: `tou2d_bot_behavior_test` (M7.1.d). Covers
`findNearestRepairTile` on empty grid / non-positive radius /
out-of-radius / inside-radius / multiple-tile-nearest-wins paths,
plus sanity bounds on the two M7.1 constants. ctest 154/154 green;
200-tick smoke clean.

**M7.2 — Per-camera HUD ownership — LANDED 2026-05-30**

Pre-M7.2 the per-slot HUD lived in world-space debug geometry
without a per-camera filter, so every primitive showed up in every
camera whose frustum contained its anchor point. Close-combat
overlap was the visible symptom. M7.2 closes the hole by adding
`cameraMask` to the engine's render contract.

1. **Engine-side (M7.2.a).** `DebugLine` and `DebugPoint` in
   `include/threadmaxx/render/DebugGeometry.hpp` gain a public
   `std::uint32_t cameraMask = 0xFFFFFFFFu` field. Bit `k` ↔
   `RenderFrame::cameras[k]`; default all-ones = "visible from
   every camera" → backward compatible with every pre-M7.2 caller.
   32-bit width matches the engine's `kMaxCameras = 32` cap.
2. **Vulkan renderer (M7.2.b).** `recordFrame` now groups debug
   vertices per camera index: a primitive with mask covering N
   cameras is emitted into N contiguous regions of the shared
   vertex buffer, tracked by per-camera `(firstVertex, count)`
   ranges on `PerFrame`. `recordCamera` draws only its own range
   via `vkCmdDraw(.., firstVertex = first[i] * 2, .., count[i])`.
   The K-way duplication of shared (all-ones) primitives is fine
   at typical HUD line counts (10²); a per-vertex attribute +
   shader-cull path is documented as the escape valve if a future
   workload pushes it to 10⁶.
3. **HudSystem (M7.2.c).** Every per-slot primitive in the
   `for (s = 0; s < numHumans; ++s)` loop now sets
   `cameraMask = (1u << s)`. CameraSystem registers cameras in
   slot order (`RenderFrame::cameras[i]` IS slot `i`'s camera),
   so the bit-to-camera mapping is direct. The winner banner
   (drawn once, anchored at slot 0's follow center) stays at the
   default `0xFFFFFFFFu` so every camera that overlaps it still
   sees it — that's the desired end-of-round behavior.

Test pin: `debug_camera_mask_test` (M7.2.d). Asserts default-
construction sentinels, FIFO ordering across mixed-mask emits,
and the publish-cycle round-trip (per-system builder → engine
merge → `RenderFrame` span). Docs updated: CLAUDE.md render-
contract section, `doc/render_contract.md` "Per-camera debug
geometry" subsection, `tests/COVERAGE_AUDIT.md` coverage row.
ctest 155/155 green; 200-tick smoke clean.

**M7.3 — Visual polish (ParticleSystem) — LANDED 2026-05-30**

Two ParticleSystem additions, both game-side, no engine surface
touched. The pre-M7.3 plan assumed `Particle::Thrust` already
existed and the start-color hex used the legacy `0xAARRGGBB`
convention — neither was true; both were resolved in the landing
patch.

1. **§5.1 thruster plume (M7.3.a).** New `Kind::Thrust` enum
   value (the pre-existing kinds were Debris / Smoke / Spark);
   zero gravity + faster alpha decay (`kThrustAlphaExp = 1.6`) so
   the trail reads as a wisp rather than a debris stream. New
   public method
   `ParticleSystem::emitThrusterParticle(x, y, vx, vy)` spawns
   one particle per call with TTL 12-18 ticks. In
   `buildRenderFrame`, the Thrust branch overrides the stored
   `rgb` with `thrustColorForAge(frac)` — a per-channel lerp from
   `kThrustColorHot = 0xFF40AAFFu` (yellow-orange, RGB(255,170,64))
   at `frac = 1.0` to `kThrustColorCool = 0xFF4040E0u`
   (red-orange, RGB(224,64,64)) at `frac = 0.0`. The hex literals
   are in the engine's documented `0xAABBGGRR` packing (see
   `unpackRGBA` in `VulkanRenderer.cpp`); the pre-M7.3 TOU_PLAN
   start-color `0xFFFFAA40` was in the legacy `0xAARRGGBB`
   convention and re-encoded to `0xFF40AAFFu` on landing.

2. **§5.1 wire (M7.3.b).** `MovementSystem` gains a borrowed
   `ParticleSystem*` setter (same pattern as ShipLifecycle /
   BulletShip / BulletTerrain / RepairPickup), a `tickPhase_`
   counter bumped each `update()`, and a
   `kThrustEmitInterval = 3` constant so each actively-thrusting
   ship emits one puff every 3 ticks (~20/sec at 60 Hz). Emit
   point is `ship_pos - forward * 12 wu` so the puff sits behind
   the engine; spawn velocity is `-forward * kThrustEjectSpeed`
   (`= 90` wu/s) so the trail streams cleanly. Only forward
   thrust (`in.thrust > 0`) emits — reverse is a tactical brake,
   no plume.

3. **§5.2 damage smoke (M7.3.c).** New public method
   `ParticleSystem::emitDamageSmoke(x, y)` spawns one dark-gray
   smoke puff (color `0x00606468u`, same family as the
   death-explosion smoke; reuses `Kind::Smoke` so the existing
   drag + rise integration applies; TTL 30-50 ticks vs the
   60-90 of explosion smoke so the pool isn't saturated by a
   continuously-damaged ship). Static helper
   `damageSmokeInterval(hpFrac)` returns 0 above
   `kDamageSmokeFracThreshold = 0.4`; below threshold it lerps
   linearly from ~30 ticks/puff at the threshold to ~3 ticks/puff
   at `hpFrac = 0`. `ShipLifecycleSystem` (the existing damage
   observer) polls each alive ship per tick — `if interval > 0
   && (tickPhase + row) % interval == 0 emitDamageSmoke(...)`.
   The `+ row` offset staggers simultaneous puffs across
   consecutive ticks so multiple damaged ships don't all fire on
   the same one.

Test pin: `tou2d_particles_test` (M7.3.d). Pins
`thrustColorForAge` endpoints + per-channel monotonic lerp +
boundary clamping; pins `damageSmokeInterval` threshold gate +
monotonicity (40-sample sweep) + documented [3, 32] band; pins
the emit + `buildRenderFrame` round-trip — a freshly-emitted
Thrust particle hits the wire with `colorRGBA & 0x00FFFFFF` ==
`kThrustColorHot`, a damage-smoke puff with `0x00606468u`.
ctest 156/156 green; 200-tick smoke clean.

**M7.4 — Faction system (bot ally targeting) — LANDED 2026-05-30**

Game-side only; no engine surface touched. Adds the first ally
relationship in the demo via a single `uint8_t factionId` field
threaded through LocalPlayer, MatchSetup, the spawn path, and two
existing systems.

1. **§a engine-data field.** `LocalPlayer` gains a `factionId` byte
   (replaces one byte of the `_pad[6]` padding so the POD stays at
   8 bytes; `static_assert` unchanged). Default = `kFactionAuto`
   sentinel (0xFF). Same sentinel on `PlayerSlotSetup.factionId`
   (replaces one byte of its own `_pad[2]`). Sentinel semantics:
   "use slot as faction" — TouGame::spawnShip rewrites it to `slot`
   at spawn time, so default-init reproduces the pre-M7.4 free-for-
   all (every slot in its own unique faction).

2. **§b spawn plumbing.** `spawnShip` takes a new `factionId`
   parameter and stamps it on `LocalPlayer`. TouGame::onSetup
   resolves per-slot overrides exactly the same way it resolves
   shipKind/palette (sentinel → auto; anything else → pinned).

3. **§c BotControlSystem ally skip.** `ShipPos` gains a `factionId`
   byte; pass 1 of `preStep` captures it from the LocalPlayer span.
   The engage-target loop in pass 2 rejects candidates with matching
   `factionId` before the geometry check. Effect: a bot never picks
   an ally as its nearest target, so the orbit/aim/wobble pipeline
   never aims at one. Same-faction-only-in-range bots fall through
   to wander instead of orbiting an ally.

4. **§d WeaponFireSystem friendly-fire suppression (bots only).**
   New pre-pass collects positions + `factionId` of every live
   LocalPlayer ship into a fixed-size `std::array<AllyPos, 16>`
   (matches BotControlSystem's `live` cap). For each firing ship,
   if it's a bot AND `botShotHitsAlly` returns true (any same-
   faction ally inside the forward cone — half-angle
   `kFriendlyFireArcRad ≈ 17°`, range `kFriendlyFireRangeWU = 220`),
   both the basic and special branches are gated off this tick.
   Humans bypass the check entirely (friendly fire stays a game
   rule for manual aim, per the M7.4 design note). The arc is
   deliberately wider than BotControlSystem's `kFacingFire` (~10°)
   so the suppression is conservative — false-positive suppressions
   are preferable to blue-on-blue. Cost: O(N) per shot at N ≤ 16
   live ships.

5. **§e PlayerSetup UI Faction column.** Adds
   `MatchSetupKnob::SlotFaction` and per-slot row in
   `kPlayerSetupRows`. Per-slot stride goes from 6 → 7; total rows
   from 25 → 29; the trailing Back row moves to index 28. Cycle is
   Auto + 4 factions = 5 positions (`kFactionCycleSize`), matching
   the 4 keyboard-eligible slots so any two of them can be paired
   into the same faction. Formatter renders "Auto" / "F0" .. "F3".
   `tou2d_player_setup_test` updated to match the new stride /
   indices and adds a Faction-cycle block.

6. **§f test pin: `tou2d_faction_test`.** Eight scenarios on
   `botShotHitsAlly` (straight-ahead ally → suppressed; different
   faction → fires; out-of-range / out-of-arc / behind → fires;
   self-row skip; multi-ally OR semantics; empty list) plus
   defaults / sentinel-correctness pins for `LocalPlayer.factionId`
   and `PlayerSlotSetup.factionId` and band-checks on the arc /
   range tunables vs BotControlSystem's engagement constants. Lives
   alongside `tou2d_particles_test` in `tests/CMakeLists.txt`. The
   refactor exposing `AllyPos` + `botShotHitsAlly` +
   `kFriendlyFireArcRad` / `kFriendlyFireRangeWU` via
   `WeaponFireSystem.hpp` (formerly anonymous-namespace bits)
   makes the helper directly unit-testable without an engine
   harness.

Files touched: `DemoTypes.hpp`, `MatchSetup.hpp`,
`BotControlSystem.{hpp,cpp}` (only .cpp), `WeaponFireSystem.{hpp,cpp}`,
`UISystem.{hpp,cpp}`, `TouGame.cpp`,
`tests/tou2d_player_setup_test.cpp`, `tests/tou2d_faction_test.cpp`
(new), `tests/CMakeLists.txt`, this plan.

Verification: build clean under
`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON`; ctest all green; 200-tick
smoke clean.

**M7.5 — Pickup framework split (repair base vs kit) — LANDED 2026-05-30**

Pre-M7.5 the terrain grid carried one `Attribute::Repair` per cell
and `RepairPickupSystem` consumed it on touch (heal + cycle special +
clear). M7.5 splits that into two lanes — a non-consuming tile base
that regenerates per tick, and an entity-based collectible kit that
respawns on a timer. Six sub-batches:

1. **Data model (M7.5.a).** `DemoTypes.hpp` gains:
   * `Attribute::RepairBase = 3` (renamed from `Repair`; same byte
     so existing snapshots and procedurally-generated levels load
     unchanged).
   * `PickupKind { RepairKit = 0, Count }` + `kPickupKindCount`.
   * `Pickup { kind:u8, state:u8, respawnIn:u16, _pad[4] }` —
     8-byte user component. `state` is 0=active, 1=respawning.
   * `PickupSpec { respawnIntervalTicks:u16, effectMagnitude:u16,
     _pad[4] }` — 8 bytes; `pickupSpecAt(PickupKind)` constexpr
     accessor into `kPickupSpecs` (single-source-of-truth table
     indexed by `PickupKind`).
   * `kRepairBaseHpPerTick = 1.0f` (per-tick regen rate; max-HP
     ship refills in ~3 s at 60 Hz).
   * `UserComponentIds::pickup` field.

2. **Terrain migration (M7.5.b).** `TerrainGrid::setRepair` →
   `setRepairBase`. Call-site sweep: `ProceduralLevel.hpp` (proc
   sprinkles), `TouGame.cpp` (synthetic arena spokes), `main.cpp`
   (background painter), `BotControlSystem.cpp` (nearest-repair
   scan), tests.

3. **RepairPickupSystem rewire (M7.5.c).** Non-consuming RepairBase
   semantics. New behaviour:
   * Per-tick `Ship::currentHp += kRepairBaseHpPerTick` (clamped
     to `maxHp`) while a ship's AABB overlaps a RepairBase tile.
   * Edge-triggered special-cycle: first tick a ship newly enters
     a base, advances `WeaponLoadout::specialKind` once + refills
     `specialAmmo` to the new spec's magazine. Doesn't re-trigger
     while the ship stays put.
   * Audio cue + particle burst fire on entry only.
   * Tile DOES NOT consume — no `grid->clear` / no destroyCb.
   * Per-ship entry-edge state lives in `onBasePrev_` (entity
     index set) on the system; swapped at end of `update()`.

4. **RepairKitSystem (M7.5.d).** New system `tou2d.repairKit`.
   Two-pass `update()` inside `ctx.single`:
   * Pass 1 snapshots up-to-16 live ships' positions / Ship /
     WeaponLoadout pointers from non-disabled chunks.
   * Pass 2 walks every chunk carrying the `Pickup` bit (no
     `DisabledTag` filter — respawning kits must tick down even
     while hidden). For `state == 0`: AABB-test against each
     snapshotted ship, on hit apply `pickupSpecAt(kind)` effect
     (RepairKit → heal `effectMagnitude` HP + cycle special), set
     `state = 1`, write `respawnIn = respawnIntervalTicks`, attach
     `DisabledTag` (kit hidden during cooldown). For `state == 1`:
     decrement `respawnIn`; on zero, flip to `state = 0` and
     remove `DisabledTag`.
   * `kKitHalfExtent = 8 wu` so a near-miss still grabs (kits are
     sparse vs. tiles).
   * Public per-step counters: `pickupsThisStep`, `pickupsTotal`,
     `respawnsThisStep`.
   * Future kinds: switch on `kind` at the apply site — no virtual
     interface, per the M7.5 data-driven plan.

5. **TouGame integration (M7.5.e).** Registers `Pickup` user
   component after `ShipSpriteRef`. Spawns `RepairKitSystem`
   alongside the existing `RepairPickupSystem` (disjoint chunks —
   `Pickup` chunks are never `Ship` chunks, so they coexist in the
   same wave without conflict). Wires `setParticleSystem` for the
   kit FX. No kits spawned yet — the framework lands; the demo
   keeps its RepairBase spokes from the synthetic arena and 12
   sprinkled bases from proc-gen. Kit-spawning + render asset is
   intentionally deferred to a follow-up batch so M7.5 stays a
   clean "framework + behaviour split" landing.

6. **Tests (M7.5.f).** `tou2d_repair_pickup_test` updated:
   * `Attribute::Repair` → `Attribute::RepairBase` everywhere
     (enum-byte 3 pin retained).
   * `setRepair` → `setRepairBase`.
   * Per-tick regen check: clamps to `maxHp` from 149.5 + 1.0; a
     non-full ship gains exactly `kRepairBaseHpPerTick`.
   * Procedural determinism + count checks retargeted to
     `Attribute::RepairBase`.
   `tou2d_bot_behavior_test` `setRepair` → `setRepairBase` sweep
   (5 sites). New `tou2d_pickup_framework_test` pins six contract
   sections: Attribute byte, PickupKind cardinality, Pickup POD
   size + defaults, PickupSpec catalogue values + sensible band,
   regen-vs-kit-heal tunable band, and a respawn-countdown
   simulation that exercises the state-1 → state-0 transition.

Files touched: `DemoTypes.hpp`, `RepairPickupSystem.{hpp,cpp}`,
`RepairKitSystem.{hpp,cpp}` (new), `TouGame.cpp`,
`BotControlSystem.{hpp,cpp}`, `ProceduralLevel.hpp`, `main.cpp`,
`examples/tou2d/CMakeLists.txt`, `tests/CMakeLists.txt`,
`tests/tou2d_repair_pickup_test.cpp`,
`tests/tou2d_bot_behavior_test.cpp`,
`tests/tou2d_pickup_framework_test.cpp` (new), this plan.

Verification: build clean (no warnings); ctest 158/158 green; 200-
tick smoke (`./build/examples/minimal/threadmaxx_minimal 200`)
ends with `[ConsoleRenderer] shutdown after 201 frames`.

**M7.6 — Water mechanic (buoyancy in MovementSystem) — LANDED 2026-05-30**

Pre-M7.6 `Attribute` was `Air`/`Solid`/`Damage`/`RepairBase` —
no water concept. M7.6 adds a non-blocking traversable cell that
ships pass through but feel mechanically.

1. **Data model (M7.6.a).** `DemoTypes.hpp` gains:
   - `Attribute::Water = 4` (stable byte position past
     `RepairBase = 3`).
   - `TerrainGrid::setWater(cx, cy)` — non-blocking flip
     (`hp = 0`, attr = Water; bullets and terrain-collision treat
     it as Air-equivalent for solidity).
   - Tunables: `kWaterBuoyancyFraction = 0.7f` (gravity scale
     when fully submerged is `1 - 0.7 = 0.3`, sink-but-slowly),
     `kWaterDragPerSecond = 1.6f` (first-order drag on top of
     air damping), `kWaterTileColor = 0xFFB87038u` (ABGR
     translucent blue for the background painter).

2. **MovementSystem integration (M7.6.b).** New borrowed
   `setTerrainGrid(const TerrainGrid*)`. Each integrate step
   samples the ship's center + 4 cardinal neighbors at one
   ship-half offset (5 samples total) for `Attribute::Water`;
   `wetness = wet_samples / 5` is the smooth-blend fraction
   in `[0, 1]`. Applied to:
   - Gravity: `v.y -= kGravityAccel * (1 - wetness *
     kWaterBuoyancyFraction) * dt`.
   - Drag: after the normal air damping, when `wetness > 0`,
     multiply both linear axes by `exp(-kWaterDrag * wetness * dt)`.
   Null-grid is a no-op (collapses to pre-M7.6 behaviour) so
   headless tests don't need to wire a grid.

3. **TouGame wiring (M7.6.c).** `setTerrainGrid(&grid_)` on the
   movement system at registration time. Synthetic arena
   sprinkles two 5×3 puddles offset from the spawn axes so the
   200-tick smoke run grazes a Water cell during normal AI
   behaviour without being so close to spawn that the round
   trivially exits inside one.

4. **Background painter (M7.6.d).** New Water arm in
   `main.cpp`'s level-load synth painter — translucent blue
   (`r=56, g=112, b=184`) so water cells read as water at load.

5. **Tests (M7.6.e).** New `tou2d_water_test`. Six sections:
   - `Attribute::Water = 4` byte pin (forward-compat).
   - `setWater` round-trip: attr → Water, hp stays 0, OOB silent.
   - Tunable bands: buoyancy ∈ (0, 1), drag ∈ (0, 5).
   - Integration semantics, mirroring `MovementSystem`'s
     gravity → air-damp → water-damp order:
     - air-only matches expected `-g·dt` step delta,
     - fully-wet single-tick velocity strictly greater (less
       downward) than air-only,
     - after 600 ticks (10 s @ 60 Hz) wet terminal-fall speed
       is at least 25% slower than air-only,
     - half-wetness strictly between (smooth-blend
       monotonicity).

6. **Deferred follow-ups.** Procedural water sprinkle
   (parallel to `repairTileCount`) is OUT — would expand
   `ProceduralLevelConfig` past its 8-byte replay-header
   reservation. Lands when the replay header version bumps.
   `BulletTerrainSystem` does not currently special-case Water
   (bullets fly through, treated as Air via `hp == 0`); a
   future polish round can add splash particles. Smooth-blend
   wetness is also not piped into the thruster plume audio
   yet — covered by a future audio polish batch.

Verification: ctest green (159/159 with the new
`tou2d_water_test`; +1 over M7.5). 200-tick headless smoke
clean (`[ConsoleRenderer] shutdown after 201 frames`).

**M7.7 — Acceptance closeout**

Mirror the M6.10 acceptance checklist for the M7 batches:
- [ ] AI low-HP behavior tactical (M7.1)
- [ ] AI no longer stalls / wanders correctly (M7.1)
- [ ] HUD per-camera ownership clean across all layouts (M7.2)
- [ ] Thruster + damage smoke read correctly under playtest (M7.3)
- [ ] Allied AI does not target same-faction members (M7.4)
- [ ] Repair base vs kit split is playable + extensible (M7.5)
- [ ] Water is traversable with smooth transitions (M7.6)
- [ ] No M6 acceptance criteria regressed
- [ ] Every M7 batch shipped with a test in the same PR

#### M7 — Engine-side prereqs

| Item | Status |
|---|---|
| `cameraMask` on `DebugLine` / `DebugPoint` | LANDED 2026-05-30 as part of M7.2 |
| New `Pickup` user component + `PickupKind` enum | M7.5 prereq; sized as part of the split |
| New terrain `Attribute::Water` enum value | LANDED 2026-05-30 as part of M7.6 |

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
| Text rendering (M6) | No font system in the engine today; HudSystem uses sprite-digits only | M6.0 ships a TTF runtime bake via `stb_truetype.h` + `textPrintf` example-side. Drop-in swappable font (replace the .ttf, pixel size + codepoint range from `FontConfig`). SDF / hinting / kerning are opt-in later. |
| UI compositor cost (M6) | Many small `DrawItem`s per tick (each glyph is one quad) at 4 viewports could push the renderer's instance buffer | M6 budget gate: HudSystem + UISystem combined < 100 µs / tick. If breached, batch glyphs into a single instanced draw per text run. |
| Settings persistence portability (M6.5) | Host-endian POD on disk → BE host loads LE-saved file produces garbage | v1 scope is Linux x86_64 only (same as the perf box); document the limitation. Cross-host config sync is out of scope. |
| Pause + replay interaction (M6.4) | Recording on paused frames would desync the input stream | `Replay::onStep` skips paused steps; `Engine::setPaused(true)` already zeros per-tick stats so the contract is clean. Pinned by a M6.4 replay round-trip test. |

---

## 9. What to commit when

Per the standing project commit policy (don't autonomously commit; user authorizes per artifact):

- **This document** (`TOU_PLAN.md` + the `examples/tou2d/` directory containing it) — ready to commit on user authorization. No code yet.
- **Milestone 1** — first code-bearing batch. Bundles renderer choice + ship-thrust loop. Single PR / commit.
- **Milestone 2** — second batch. Bundles terrain + Tier 1 importer.
- Each subsequent milestone — its own batch.
- **Milestone 6** — each M6.x batch its own commit (the M5 cadence: one batch == one commit, each with its own test + headless smoke). M6.0 is the lone batch that may touch multiple game-side systems at once (font + compositor + KeyMap) since they're tightly coupled; subsequent M6.x batches are self-contained.

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
