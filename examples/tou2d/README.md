# tou2d

A 2D arena combat game built on threadmaxx, modeled after the 2002 freeware
*TOU* by GigaMess (Hannu Kankaanpää). 1–4 humans + up to 63 bots brawl in a
top-down arena with destructible tile terrain, repair pads, multiple weapons,
and per-human split-screen.

The project doubles as a **proof of engine breadth**: every threadmaxx
subsystem (work-stealing job system, archetype storage, command-buffer
commit, event channels, the resource registry, the render-frame builder)
gets exercised in a real game shape, not just a microbenchmark.

This README is the entry point. Three sibling docs cover the rest:

- [`CHANGELOG.md`](CHANGELOG.md) — chronological per-batch landings (M4 → M7).
- [`TOU_PLAN.md`](TOU_PLAN.md) — live plan: deferred / open / future work only.
- [`TOU_RE.md`](TOU_RE.md) — reverse-engineering notes for the original
  `.lev` / `.SHP` formats + asset inventory + gameplay reference.

> **First time touching the project?** The repository-root
> [`COMPLETE_BEGINNERS.md`](../../COMPLETE_BEGINNERS.md) walks you through
> building a tou2d-style game from zero, explaining every game/ECS concept
> along the way. Start there if "ECS" and "command buffer" feel foreign.

---

## Quick start

```sh
# From repo root, configured + built per the engine README:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j

# Headless smoke (no renderer; verifies the sim wiring):
./build/examples/tou2d/threadmaxx_tou2d 200            # 200 ticks then exit

# Interactive (requires the Vulkan renderer — see below):
./build/examples/tou2d/threadmaxx_tou2d                # default 1 human + 3 bots
./build/examples/tou2d/threadmaxx_tou2d --humans=2 --bots=2
./build/examples/tou2d/threadmaxx_tou2d --humans=4 --bots=10 --level <path>
```

The tou2d binary builds whenever `THREADMAXX_BUILD_EXAMPLES=ON` (the project
default) and the Vulkan example renderer is available. It's silently skipped
when Vulkan 1.3 + GLFW + `glslc` aren't found — same gating as `rpg_demo`.

### CLI flags (host-level)

| Flag | Meaning | Default |
|---|---|---|
| `--humans=N` | Number of human players (1..4). | `1` |
| `--bots=M` | Number of AI ships (0..63). | `3` |
| `--mode=<m>` | Match mode: `deathmatch` / `last-standing`. | `deathmatch` |
| `--level <path>` | Imported level dir (Tier 0/1 output) or `synthetic`. | `synthetic` |
| `--ticks=N` | Bounded headless smoke. Exits after N ticks. | unbounded |

The combined minimum is `humans + bots >= 2` — a match always has at least
two ships.

---

## Controls

Default bindings mirror the original TOU 4-player-on-one-keyboard layout
documented in `toudoc_controls.htm`. Every binding is rebindable via the
in-game **Options → Controls** screen (M6.5, persisted to `settings.dat`).

| Action | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| Thrust | ↑ | W | I | Numpad 5 |
| Reverse | ↓ | S | K | Numpad 2 |
| Turn left | ← | A | J | Numpad 1 |
| Turn right | → | D | L | Numpad 3 |
| Basic weapon | RShift | Tab | B | Numpad 7 |
| Special weapon | RCtrl | Q | V | Numpad 8 |
| Menu / launch-all | `/` | `1` | N | Numpad 9 |

**Emergency teleport** — holding *turn left* + *turn right* simultaneously
for ≥ 1 s triggers a teleport that costs energy. Faithful port of the
original mechanic.

**System keys** — `Esc` opens the pause menu; `~` opens the debug overlay
(M6.9); `F5` quick-save, `F9` save-diagnose (when implemented per host).

---

## What it shows off

The game is small and self-contained, but every milestone exercises a
different slice of the engine. Pinned summary:

| Engine surface | tou2d feature that exercises it |
|---|---|
| `forEachChunk<…>` hot path | `ParticleSystem` (thousands of particles per tick), `BulletShipCollisionSystem`, `MovementSystem` |
| `CommandBuffer` value-only setters | Per-tick velocity/transform updates from movement + gravity |
| `CommandBuffer` mask-change setters | Pickup grab / ship death (`+/-DisabledTag`), `addTag`/`removeTag` for transient states |
| `EventChannel<T>` | `BulletHit`, `ShipDied`, `Pickup`, `BlockBroken`, `AudioPlay` |
| `UserComponent<T>` | `Ship`, `Bullet`, `Particle`, `Pickup`, `LocalPlayer`, `BotAI`, `TerrainBlock` (every game-side POD) |
| `ResourceRegistry` + `IResourceLoader` | Sprite atlases, sound bank, font atlas, level JPG + TGA |
| `RenderFrameBuilder` per-system slice | `CameraSystem`, `HudSystem`, `ParticleSystem`, `ToastRenderSystem`, `DebugOverlaySystem` |
| Multi-camera + per-camera `cameraMask` | 1–4 human split-screen with per-viewport HUD that doesn't leak (M7.2) |
| Wave scheduler (R/W declared) | Movement / Projectile / Particle systems share a wave; Damage / Respawn sequence cleanly |
| `Engine::setPaused` + replay safety | Pause menu freezes the world; replay capture skips paused ticks |
| `Config::singleThreadedCommit` (default) | Demo-realistic command volumes commit cleanly under serial commit |

---

## Build it from source

`examples/tou2d/CMakeLists.txt` wires the binary; the parent
`CMakeLists.txt` opts it in via `THREADMAXX_BUILD_EXAMPLES`. CMake silently
skips it if the Vulkan renderer is not built, matching `rpg_demo`'s gating.

Two opt-in importer CLIs build unconditionally when tou2d builds — they
have no renderer dependency:

```sh
./build/examples/tou2d/tou2d_import_lev <path/to/foo.lev> <output_dir>
./build/examples/tou2d/tou2d_import_shp <path/to/foo.SHP> <output_dir>
```

Both are documented under "TOU asset compatibility" below.

### Sanitizer hygiene

The tou2d binary is **excluded** from ASAN / UBSAN / TSAN sanitizer trees,
matching the engine's standing convention ("sanitizers run engine-side
correctness only"). The headless smoke (`./threadmaxx_tou2d N`) is a clean
exit signal; the unit tests under `tests/` (every `tou2d_*` target) run
under the regular CTest harness alongside the engine suite.

---

## TOU asset compatibility

The project is **partially compatible** with the original *TOU* on the
asset side. Three compatibility tiers, cheapest first:

### Tier 0 — Asset-format pass-through

Drop-in support for the open formats *TOU* already used:

- **GG procedural-level themes.** Drop a `TOU/ggstuff/<theme>/` directory
  into `examples/tou2d/assets/themes/<theme>/` and the procedural-level
  generator picks it up. The original's `info.txt` `/KEY value` schema
  parses verbatim.
- **24-bit uncompressed `.tga` attribute maps + `.jpg` visual layers.**
  Loaded via `stb_image`; pixel color → tile attribute per the
  `colors.png` legend.
- **`.wav` / `.ogg` audio.** Drop `TOU/sfx/*.wav` and `TOU/music/*.ogg`
  into `examples/tou2d/assets/sounds/` and `assets/music/`. Backend
  plays directly.

### Tier 1 — Binary `.lev` import

`tou2d_import_lev <foo.lev> <output_dir>` extracts the embedded JPEG +
TGA + config into a Tier 0 asset bundle. The 4 shipped originals
(`desert / jungle / minibase / woods`) round-trip cleanly.

### Tier 2 — Binary `.SHP` import

`tou2d_import_shp <foo.SHP> <output_dir>` extracts the per-rotation sprite
frames from a `.SHP` file using the reverse-engineered format (see M4.7c
in [`CHANGELOG.md`](CHANGELOG.md) for the layout details). The 9 shipped
originals all import. Per-team color tinting lands in M4.7d.

**Hard line.** Nothing from `TOU.exe` is disassembled or copied. All
compatibility flows through the on-disk asset formats, which are public
via `toudoc_makelev.htm`. The `TOU/` install directory itself is treated
as non-redistributable — it's an inspection-only artifact on the local box.

---

## Project layout

```
examples/tou2d/
├── CMakeLists.txt           — wires the binary + opt-in CLI tools
├── README.md                — this file
├── CHANGELOG.md             — per-batch landings (M4 → M7)
├── TOU_PLAN.md              — live plan (deferred / open / future)
├── main.cpp                 — entry point + CLI parse + host loop
├── TouGame.{hpp,cpp}        — IGame implementation
├── DemoTypes.hpp            — UserComponent POD definitions + constants
├── MatchSetup.hpp           — match composition struct (host ↔ menu)
├── Settings.hpp + SettingsIo.cpp — persisted settings.dat
├── *System.{hpp,cpp}        — one pair per ISystem
├── ui/                      — UI screens (menu, options, scoreboard)
├── scripts/                 — Python reverse-engineering helpers
├── tou2d_import_lev.cpp     — Tier 1 binary level importer CLI
└── tou2d_import_shp.cpp     — Tier 2 binary ship-sprite importer CLI
```

### Notable systems (alphabetical)

| System | Purpose |
|---|---|
| `AudioSystem` | Drains `AudioPlay` events into miniaudio. |
| `BotControlSystem` | Bot AI state machine (engage / wander / retreat). |
| `BulletHomingSystem` | Per-tick course correction for guided projectiles. |
| `BulletShipCollisionSystem` | Bullet ↔ ship hits → damage + score. |
| `BulletTerrainSystem` | Bullet ↔ tile hits → terrain damage. |
| `CameraSystem` | Multi-camera split-screen layout; per-human follow + clamp. |
| `DebugOverlaySystem` | F3 / `~` HUD overlay (FPS, ents, commitHash, top-3 systems). |
| `HudSystem` | Per-viewport HUD (HP bar, ammo strip, identity badge). |
| `InputSystem` | Raw input → `PlayerInput` per slot. |
| `LevelLoader` | Loads Tier 0/1 level dir or generates a procedural one. |
| `MovementSystem` | Thrust + gravity + drag + water buoyancy/drag. |
| `ParticleSystem` | Thrust plume, damage smoke, explosions, debris. |
| `ProjectileSystem` | Bullet lifetime + gravity-applied trajectories. |
| `RepairKitSystem` + `RepairPickupSystem` | Repair-base regen vs. one-shot kit pickup. |
| `RoundRestartSystem` | Round-end detection + restart eligibility. |
| `ShipLifecycleSystem` | Spawn / respawn / death state transitions. |
| `TerrainCollisionSystem` | Ship ↔ tile collision response. |
| `ToastRenderSystem` | Kill-feed / pickup-feed / system-message toasts. |
| `UISystem` | Menu state machine (main / setup / pause / options / scoreboard). |
| `WeaponFireSystem` | Trigger handling + per-weapon spawn rules. |

---

## Status

M4 through M7 are complete. The M7 closeout swept every M7.1–M7.6
acceptance criterion through its pinning test (159/159 green). See
[`CHANGELOG.md`](CHANGELOG.md) for the per-batch history and
[`TOU_PLAN.md`](TOU_PLAN.md) for what's tracked but not yet built.

---

## See also

- [`COMPLETE_BEGINNERS.md`](../../COMPLETE_BEGINNERS.md) — beginner guide to
  building a tou2d-style game from scratch.
- [`../../ARCHITECTURE.md`](../../ARCHITECTURE.md) — engine design overview.
- [`../../CLAUDE.md`](../../CLAUDE.md) — contributor playbook.
