# TOU_PLAN — live plan

Forward-looking plan for `examples/tou2d/`. Historical landings live in
[`CHANGELOG.md`](CHANGELOG.md); the user-facing overview lives in
[`README.md`](README.md). This document is **the live plan only**:
deferred work, open questions, risks to watch, and out-of-scope items.

Distilled out of the original multi-thousand-line plan on 2026-05-31; the
full per-batch history (rationale + landed mechanics + acceptance
criteria) is now in `CHANGELOG.md`. The asset-format archaeology
(`.lev` / `.SHP` reverse engineering, the `colors.png` legend, the GG
theme layout, the M4.7c/d body-decoder journey) now lives in
[`TOU_RE.md`](TOU_RE.md).

---

## 1. Status

- **M4 (bots + Tier 2 sprite import + audio)** — COMPLETE ✅
- **M5 (polish + replay)** — COMPLETE ✅
- **M6 (GUI / UX)** — COMPLETE ✅
- **M7 (polish pass + playtest debt)** — COMPLETE ✅

Engine-side scope: still zero touches to `include/threadmaxx/` or `src/`
beyond the M7.2 `cameraMask` addition on `DebugLine` / `DebugPoint` (a
backwards-compatible field with an all-ones default).

---

## 2. Outstanding deferred work (M7 close)

Surveyed 2026-05-31 during M7.7 closeout. None block acceptance; each is
its own focused follow-up.

### 2.1 Engine-restart-with-MatchSetup primitive — **DONE (2026-06-18)**

Shipped in two waves; the plan's snapshot at M7.7 closeout was already
stale by then but only audited + closed out in N1 (2026-06-18):

- **M6.2 `StartMatch` apply** — wired via `restartMatch` lambda in
  `main.cpp`. Drains `pendingStartMatch_`, tears the engine down
  (`engine.shutdown` → `game.setMatchSetup` → `engine.initialize`),
  rebuilds the renderer-side textures + sprite-compositor sizing for
  the new world.
- **M6.4 `RestartMatch` apply** — same drain, same lambda.
- **M6.6 `Rematch` apply** — same drain, same lambda (`prevRoundEnded`
  rising-edge tracker reset alongside).
- **N1 — MainMenu "Continue" enablement** (2026-06-18). `UISystem`
  carries `resumableMatchInFlight_` + a runtime mirror of the MainMenu
  rows. Flipped on by `Pause → Return to main menu`, off by every
  restartMatch cycle. Continue accept dismisses MainMenu; the engine's
  `paused()` bind unfreezes the same world the user left. Pinned by
  `tests/tou2d_continue_enable_test.cpp`.

What was NOT shipped: the original plan called for a single
`Engine::restartWith(MatchSetup)` engine-public primitive. The current
implementation lives entirely in the host as the `restartMatch`
lambda, which is enough for the four call sites and keeps the engine's
public surface unchanged. If a third-party host needs the same
behaviour, the lambda is straightforward to lift into an engine-public
method later — but no caller has asked for that yet.

### 2.2 M7.5 kit spawning — **DONE (2026-06-18, N2)**

Both pieces landed in N2:

- **Spawn-time placer.** `TouGame::onSetup` now seeds kits scaled with
  ship count (1 per 2 ships, floor 2, cap 12). Placement is via
  `sampleRandomRespawn` against the populated terrain grid so it works
  uniformly across synthetic-arena, procedural-generated, and imported
  `.lev` paths. The shared `spawnRng` preserves per-`MatchSetup`
  determinism.
- **HUD glyph.** `HudSystem` walks `Pickup` chunks in `update()`,
  skipping `DisabledTag` (respawning) entries, and emits a cyan "+"
  cross in `buildRenderFrame()`. Distinct from the green `RepairBase`
  tile painted into the terrain JPG. Latched count capped at
  `kMaxKitGlyphs = 64`.

Pinned by `tests/tou2d_kit_spawn_test.cpp`.

### 2.3 M7.6 procedural water sprinkle (still blocked)

The two non-blocked polish items shipped in N3 (2026-06-18):

- **Water-splash particles + bullet consumption** — `BulletTerrainSystem`
  emits a cyan splash + `kSoundWaterSplash` audio + destroys the bullet
  on water entry.
- **Wet-thrust splash + audio** — `MovementSystem` samples wetness at
  the engine point and emits scaled splash particles + a rate-limited
  splash sound when wetness ≥ `kWetThrustThreshold`.

What's still blocked: bumping `ProceduralLevelConfig` past its 8-byte
replay-header reservation. Once unblocked: add `waterCellCount` knob
parallel to `repairTileCount`, sprinkle Water cells in the generator.
Lands when the replay header version bumps (own batch — affects format
compatibility).

### 2.4 M6.5 sub-screens — **partially done (2026-06-18, N4)**

Three of the seven settings now fan out at restart-time via
`TouGame::setSettings`:

- ✅ **`gameplay.damageScale`** → `BulletShipCollisionSystem::setDamageScale`
- ✅ **`gameplay.respawnDelayTicks`** → `ShipLifecycleSystem::setRespawnTicks`
- ✅ **`controls`** → `InputSystem::setKeyMap`

Still deferred (require infrastructure the current N batch can't lift):

- **Video knobs** (fullscreen, vsync, ui_scale, resolution) — Vulkan
  swapchain rebuild + GLFW window mode change. Out of scope for the N
  batch; documented as "applies at host restart" in the Options UI.
- **Live music driver** — no driver exists; the `music` slider persists
  but has nothing to bind to.
- **`gameplay.cameraMode`** — requires `CameraSystem` to grow `Split` /
  `Follow` / `Fixed` modes; currently `numHumans` indirectly drives the
  layout.
- **Benchmark sub-screen** — trace-sink + scripted-skip toggles are
  already partially routed via existing engine APIs; full apply lands
  with the benchmark-host follow-up.

### 2.5 M6.7b HUD polish queue — **partially done (2026-06-18, N5)**

Shipped in N5:

- ✅ **Damage-tick flash on HP bar** — `HudSystem::DamageFlash[slot]`
  latches an HP-decrease across ticks; bright white overlay for
  `kDamageFlashTicks = 6` ticks. Photosensitive accessibility halves
  the flash alpha.
- ✅ **Low-ammo warning marker** — orange DebugPoint above the ammo
  strip when ammo ≤ 25% of magazine AND not reloading. Fires
  independently per row (dumbfire + special).
- ✅ **Ship-on-fire warning marker** — orange-red glyph above the HP bar
  when `0.25 < hpFrac ≤ 0.40`. Aligned with
  `ParticleSystem::kDamageSmokeFracThreshold` so the visual marker
  shows up at the same moment the ship starts trailing smoke.

Still deferred:

- **Weapon icon sprites** — needs asset work; current geometric glyphs
  read fine.
- **Identity badge** — `MatchSetup::playerSlots[].tag[3]` exists but the
  HUD has no text-rendering path (DebugText isn't supported by the
  Vulkan renderer). Would need a small icon-glyph encoding for the
  tag chars.
- **Match timer countdown** — depends on a time-cap `RoundEnded` event
  that doesn't exist yet (current modes are frag-limit DM + LSS
  survival).

### 2.6 M6.6 scoreboard depth — **DONE (2026-06-18, N6)**

`BulletShipCollisionSystem` now carries three per-slot accumulator
arrays (`deathsBySlot_`, `damageDealtBySlot_`, `damageTakenBySlot_`)
with getters + a `resetStats()` reset hook. `RoundRestartSystem`
holds a borrowed pointer and clears the accumulators on every round
restart. `TouGame::collectMatchResults` reads them into the bumped
`MatchResultsSlot` (8 → 16 bytes) so the Results screen can display
the full depth.

Display formatter changes for the Results rows deferred — this batch
wires the numbers through; the row-text layout that consumes them is
a small follow-up once the layout direction is decided.

### 2.8 NPC AI overhaul — **DONE (2026-06-19, N7)**

The "bots get stuck on terrain" playtest signal — the largest
production blocker in tou2d — fixed by adding velocity-aware unstuck
recovery and per-difficulty AI tuning.

**Root cause** (BotControlSystem audit, 2026-06-19): pre-N7 the
avoidance system rotated the nose on a wall hit but never compensated
for the linear-velocity vector already carrying the bot INTO the wall.
The `fullyBoxed` clause killed thrust but not velocity, so the bot
drifted forward into the corner anyway. Fix: stuck-trace ring + a
reverse-thrust escape branch that overrides PlayerInput with `back =
1` while peeling off the wall.

**Navmesh consideration**: `threadmaxx_navmesh` exists as a sibling
library, but tou2d's terrain is destructible (every shot rewrites
topology). A baked navmesh would go stale every tick. The fix lives
in the existing grid-based AI — make it smarter about its own
velocity vector instead of replacing the substrate.

**Difficulty levels**: `BotDifficulty` enum (Easy/Normal/Hard/Insane)
+ `BotConfig` POD presets indexed via `botConfigForDifficulty`.
Settings hookup via `GameplaySettings::botDifficulty` (co-opts the
existing 8-byte pad — no settings.dat wire-shape bump). Normal
preset reproduces pre-N7 hardcoded numbers bit-for-bit so existing
replays and tests stay valid.

Pinned by `tests/tou2d_bot_ai_n7_test.cpp` + the existing
`tou2d_bot_behavior_test` (which still passes on the Normal preset).

### 2.7 Post-v1 by design

- **M5.1 spectator camera.** A "frame all live ships" camera mode for
  replay viewing. Per-human split-screen superseded the "shared dynamic
  camera" from the original §1 scope; spectator mode is a separate
  system reading every Transform.
- **M5.8 self-effect weapons** (Teleporter / Brickwall). The original
  TOU includes self-effect weapons; v1 ships ten that exercise distinct
  engine touch-paths, leaving self-effect for post-v1 expansion.

---

## 3. Open questions

Not technical blockers; scope decisions to surface in the relevant
follow-up's kickoff.

1. **Track `TOU/` in git?** Currently visible in the working tree but the
   rationale in §4 says don't redistribute. Likely answer: add `TOU/` to
   `.gitignore`; treat it as an inspection-only artifact on the local
   box.
2. **License footer on imported assets.** When Tier 1 / Tier 2 outputs
   land in `assets/levels/`, do they get a header note "imported from
   user's TOU install; original © GigaMess 2002"? Probably yes — same
   posture as Doom WAD importers in modern source ports.
3. **Replay determinism across hosts.** Single-platform (Linux x86_64) is
   the v1 scope. Cross-host replay sync is out of scope; documented in
   the M5.4 design notes. If we ever want cross-host: pin
   `-msse2 -mfpmath=sse` everywhere + audit the few `std::math` calls
   that compilers fold to different intrinsics on different ISAs.
4. **Joystick / gamepad support.** Original TOU was keyboard-only; v1
   matches. Gamepad is a single InputSystem extension when there's
   demand.

---

## 4. License + redistribution posture

- **The new project's source** uses the same license as threadmaxx (no
  derivative work from `TOU.exe`).
- **The `TOU/` install directory** — do not redistribute. Add to
  `.gitignore`; treat as inspection-only.
- **Generated Tier 1 / Tier 2 outputs** — user's responsibility. Same
  posture as Doom WAD importers: we ship the importer, not the assets.

Hard line: nothing from `TOU.exe` is disassembled or copied as code. All
compatibility flows through the on-disk asset formats, which are public
via `toudoc_makelev.htm`.

---

## 5. Out of scope for v1

- **Split-screen of more than 4 humans.** Original TOU supported 4; we
  match. The 67-slot ceiling (`kMaxPlayerSlots`) is so we can host 4
  humans + 63 bots in the same arrays.
- **Level editor.** The source-asset workflow IS the editor — keep
  editing in Photoshop / GIMP, the engine imports.
- **Network multiplayer.** Original was local-only too.
- **More than 63 bots.** Original cap; we may go higher trivially but
  it's not the proof point.
- **Pixel-perfect re-rendering of original ship sprites.** Tier 2 import
  produces a usable atlas; pixel-perfect rendering (correct palette,
  correct anti-alias edge handling) is a stretch goal.

---

## 6. Threadmaxx-side risks to watch

Each item is an engine subsystem that hasn't been exercised in this shape
yet — listing so the relevant follow-up has the question top-of-mind.

| Risk | Where it bites | Mitigation |
|---|---|---|
| Mass particle entity count | M5.6+ explosions easily spawn thousands of `Particle` entities per frame | `forEachChunk<Particle, Transform>` + the auto `instances` lane handles this — already proven at scale in rpg_demo. |
| Tile-grid as ECS entities | 500×500 tiles = 250 k entities; `ArchetypeChunk` handles it but stitched-view rebuilds get expensive on mutation | Avoid stitched view on the hot path. Iterate via `forEachChunk` only. Keep tiles in a flat `std::vector<uint8_t>` outside ECS; only promote to entities on damage. (M2-era decision still in force.) |
| `UserComponent<T>` size budget | `Bullet`, `Particle` may want 32+ bytes; ×thousands = MB of dense storage | Aim for ≤ 16 B per Particle. Pack flags into bitfields. |
| Audio thread integration | miniaudio runs its own callback thread; `AudioSystem` must enqueue, not block | Use `EventChannel<AudioPlay>` — typed event channel is MPSC-safe. miniaudio drains on its own thread. |
| Determinism with floats | Original used integer / fixed-point physics on Win9x | Stick to `float` with `-msse2 -mfpmath=sse` (engine default on Linux). Document replay determinism as single-platform. |
| UI compositor cost (M6) | Many small `DrawItem`s per tick (each glyph is one quad) ×4 viewports could push the renderer's instance buffer | Budget gate: HudSystem + UISystem combined < 100 µs / tick. If breached, batch glyphs into a single instanced draw per text run. |
| Settings persistence portability (M6.5) | Host-endian POD on disk → BE host loads LE-saved file produces garbage | v1 scope is Linux x86_64 only. Cross-host config sync out of scope. |
| Pause + replay interaction (M6.4) | Recording on paused frames would desync the input stream | `Replay::onStep` skips paused steps; `Engine::setPaused(true)` zeros per-tick stats. Pinned by a M6.4 replay round-trip test. |
| Tunables drift | Bot retreat radius, water buoyancy, particle TTL — small constants whose right values are playtest-driven | Keep tunables as `inline constexpr` in headers visible to tests. Pin "sensible band" assertions in the corresponding test rather than exact values. (M7.6 pattern.) |

---

## 7. What ships when (commit policy)

Per the standing project commit policy: don't autonomously commit; the
user authorizes per artifact. Each batch ships as one commit with its own
test + a clean headless smoke. Per the threadmaxx convention (see
`CLAUDE.md` § "When adding a new public symbol"), any new public engine
knob lands with a `doc/performance_tuning.md` entry in the same batch.

The tou2d binary is example-side only; the M7.2 `cameraMask` field on
`DebugLine` / `DebugPoint` is the only engine-public surface change to
date in service of tou2d.
