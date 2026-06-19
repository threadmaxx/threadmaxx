# tou2d CHANGELOG

Chronological per-batch landing log. Each entry: ship date, scope, the
test that pins the contract. Deep design context (rationale, alternatives
considered, empirical numbers) lives in commit messages and in the
pre-distillation history of `TOU_PLAN.md`.

For the live plan (deferred / open / future work), see
[`TOU_PLAN.md`](TOU_PLAN.md).
For the user-facing overview, see [`README.md`](README.md).

---

## Post-M7 — playtest extensions

### N6 — scoreboard depth (2026-06-18)
Closes the §2.6 deferred item. The Results screen previously had a
`kills` column only; per-slot deaths / damageDealt / damageTaken now
land alongside it.

**Storage**: `MatchResultsSlot` bumped from 8 to 16 bytes with three
new `uint16_t` fields (`deaths`, `damageDealt`, `damageTaken`).
`MatchResults` total stride recomputed accordingly. The existing
results-screen test was updated to reflect the new size.

**Accumulators**: `BulletShipCollisionSystem` now carries three
`std::array<std::uint16_t, kMaxPlayerSlots>` arrays. In the damage-
application loop:
- `damageTakenBySlot_[victim] += applied` — always credited to the
  victim.
- `damageDealtBySlot_[firstShooter] += applied` — credited to the same
  shooter who gets kill credit on a fatal hit, mirroring the engine's
  "first shot wins" credit policy.
- `deathsBySlot_[victim] += 1` on alive→dead transition.

All three saturate at uint16 max (65,535) so long sustained rounds
can't overflow.

**Round reset**: new `BulletShipCollisionSystem::resetStats()` zeroes
all three arrays. `RoundRestartSystem` carries a borrowed pointer to
the collision system and calls `resetStats()` at the end of the
restart pass — Rematch / R-restart cycles start from zero scoreboard.

**Read-out**: `TouGame::collectMatchResults` pulls the per-slot
accumulators into the new `MatchResultsSlot` fields. Available to the
Results screen formatter for display (formatter changes deferred —
this batch just wires the numbers through).

Pinned by `tests/tou2d_scoreboard_depth_test.cpp`.

### N5 — HUD damage flash + ammo/fire warnings (2026-06-18)
Three of the M6.7b HUD polish items shipped:

- **Damage-tick flash** — `HudSystem::update()` latches the previous tick's
  hpFrac per slot in `damageFlash_[slot].prevHpFrac`. When a tick reads
  a strictly-lower hpFrac, `flashTicksLeft = kDamageFlashTicks (6)`.
  `buildRenderFrame()` overrides the HP bar fill with bright white at
  alpha proportional to `flashTicksLeft / kDamageFlashTicks`, halved
  under `accessibility.photosensitive`. Respawn / DisabledTag resets
  the latch so a freshly-spawned ship doesn't trail a stale flash.
- **On-fire warning glyph** — orange-red `DebugPoint` anchored above the
  HP bar (between badge and HP bar) when
  `kLowHpFracThreshold < hpFrac <= kOnFireFracThreshold`. Threshold
  matches `ParticleSystem::kDamageSmokeFracThreshold` so the warning
  shows up at the same moment the ship starts trailing smoke. Below
  the low-HP threshold the steady red banner takes over and the glyph
  is suppressed to avoid visual noise.
- **Low-ammo warning marker** — orange `DebugPoint` above the ammo strip
  when (a) not reloading and (b) `ammo / magSize <= kLowAmmoFrac (0.25)`.
  Fires independently for the dumbfire and special rows.

Still deferred from §2.5: weapon icon sprites (needs asset work),
identity badge (needs text or icon-glyph path; DebugText isn't
renderable today), match timer countdown (depends on time-cap
`RoundEnded` event that doesn't exist yet).

Pinned by `tests/tou2d_hud_warnings_test.cpp`.

### N4 — restart-time gameplay settings apply (2026-06-18)
Pre-N4 the Options menu let the user edit gameplay knobs which then
round-tripped through `settings.dat` but never actually fed any running
system — the values persisted but had no effect. The §2.4 plan asked
for "one read at engine-restart time inside `TouGame::setMatchSetup`"
per knob; N4 wires that pipe for the three gameplay-affecting fields
plus the controls remap.

**TouGame plumbing**: new `setSettings(const Settings&)` accessor +
private `settings_` snapshot. Host (`main.cpp`) calls it BEFORE
`engine.initialize` (initial launch) and BEFORE each `restartMatch`
cycle. The cache mirrors the UI's working copy and is re-synced from
`ui->settings()` whenever Options-back-out fires the save flag.

**Per-system setters wired in `onSetup`**:
- `BulletShipCollisionSystem::setDamageScale(float)` — multiplies every
  bullet's `damage` byte when computing the victim HP delta. Clamped to
  [0, 10] defensively. Default 1.0 reproduces pre-N4 behaviour.
- `ShipLifecycleSystem::setRespawnTicks(uint16)` — replaces the
  hardcoded `kRespawnTicks = 180` on-death stamp. Clamped to [30, 600].
- `InputSystem::setKeyMap(KeyMap)` — when installed, `preStep` polls
  GLFW against the custom map; without an install the static
  `defaultKeyMap()` (pre-N4 behaviour) stays in force.

**Out of scope for N4 (still deferred)**: video knobs (resolution /
fullscreen / vsync / ui_scale) require a Vulkan swapchain rebuild; the
music driver doesn't exist yet; `GameplaySettings::cameraMode` needs
CameraSystem to grow modes. These remain "applies at host restart"
until separate batches lift them.

Pinned by `tests/tou2d_settings_apply_test.cpp` (clamp bands, install
toggle).

### N3 — water-splash particles + wet-thrust audio (2026-06-18)
Tackled the §2.3 non-blocked items (the procedural-generator water
sprinkle stays blocked on the `ProceduralLevelConfig` header-byte
reservation).

**Bullet-on-water splash**: `BulletTerrainSystem` pre-N3 short-circuited
both Air and Water cells via the shared `hp == 0` early-out — bullets
flew through water with zero feedback. N3 splits the check: water cells
emit a `ParticleSystem::emitWaterSplash(x, y, 1.0f)` plume + a
`kSoundWaterSplash` audio event, then destroy the bullet. The original
"fly through unchanged" behaviour is gone — matches the intuition that
water absorbs projectiles. Bedrock and destructible-rock paths are
unchanged.

**Wet-thrust splash + audio**: `MovementSystem` re-samples wetness at the
thruster ejection point (not the ship centroid — a partially-submerged
nose-down ship can have a dry centroid + wet engine). When wetness ≥
`kWetThrustThreshold` (0.35), each thruster particle emit additionally
fires a low-intensity `emitWaterSplash(..., wetness)` and — at a
slower `kWetSplashAudioInterval` cadence (every 20 ticks ≈ 3 Hz) —
plays a splash sound. Pre-existing buoyancy + drag integration is
untouched.

**New audio slot**: `audio::kSoundWaterSplash = 5` (stable ID;
`kSoundCount` bumped to 6). Sound bank entry points at `wats_m.wav`,
which exists in the source TOU SFX bank (`TOU/sfx/`) — drop it into
`assets/sfx/wats_m.wav` for the splash to actually play. The graceful
missing-file path silences the slot so the demo still runs without it.

**Particle path**: `ParticleSystem::emitWaterSplash(x, y, intensity)` —
cyan/pale-blue droplets with vertical-biased launch velocity, count and
speed scaled by intensity ∈ [0, 1]. Reuses `Kind::Debris` so droplets
fall under gravity naturally.

Pinned by `tests/tou2d_water_splash_test.cpp` (enum stability,
emit-on-pool, threshold band).

### N2 — RepairKit spawn + HUD glyph (2026-06-18)
M7.5 shipped the entity-based `Pickup` user component + `RepairKitSystem`
behaviour, but `TouGame::onSetup` never actually seeded any kits — the
affordance existed in code but wasn't visible in gameplay.

N2 sprinkles a deterministic batch of kits at world start. The count
scales with total ship count (1 kit per 2 ships, floor 2, cap 12);
each kit is placed at a random Air cell sampled by `sampleRandomRespawn`
from the same shared `spawnRng` the ship seed loop already uses — so
same `MatchSetup` → same kit layout. Works against all three level
sources (synthetic arena, procedural-generated, imported `.lev`).

HUD-side: `HudSystem` now walks `Pickup` chunks in `update()`, latches
positions of active (state == 0, no `DisabledTag`) kits into
`kitPositionsXY_`, and `buildRenderFrame()` emits a cyan "+" cross at
each. Distinct visual from the green `RepairBase` TILES painted into
the terrain JPG — color + cross shape reads as "collectible kit" vs.
"static green tile." Latched count is capped at `kMaxKitGlyphs = 64`
to bound the per-frame draw budget even on dense maps.

Pinned by `tests/tou2d_kit_spawn_test.cpp` (active kits latched,
respawning kits skipped).

### N1 — MainMenu "Continue" resumability (2026-06-18)
Audit of TOU_PLAN.md §2.1 surfaced that the StartMatch / RestartMatch /
Rematch "engine-restart-with-MatchSetup" wiring had already been
implemented as the `restartMatch` lambda in `main.cpp` (history was
just stale in the plan). The remaining gap was the MainMenu "Continue"
row, which had been wired as `enabled = false` at construction and
never flipped.

Implementation: `UISystem` now carries `resumableMatchInFlight_` and a
runtime mirror of the MainMenu row table (`mainMenuRowsLive_`). The
flag flips on inside `MenuAction::ReturnToMainMenu` (Pause → Return to
main menu — the only path that surfaces a paused-and-resumable world
behind MainMenu). `MenuAction::Continue` accept dismisses the menu
(`setCurrent(UIScreen::None)`), so the host's `engine.paused()` bind
unfreezes the same world the user left. The flag stays sticky so
repeated Pause → Return → Continue cycles work. Host's `restartMatch`
lambda resets the flag to false on every fresh restart cycle (Single
Match / Start / Restart / Rematch) — the new match has no prior
paused world worth resuming.

Pinned by `tests/tou2d_continue_enable_test.cpp` (default state,
Pause→Return flip, Continue accept, host reset, disabled-accept no-op).

Engine-side scope: unchanged. The "Continue" affordance is pure
UISystem state; no engine knob touched.

### B3 — sky / parallax background layer (2026-06-01)
The `.lev` container's second embedded JPEG (`parallax.jpg` — extracted
since B1 but unused at runtime) now renders as a parallax sky behind
the destructible terrain. The original TOU game composited this layer
beneath the level art and scrolled it more slowly than the terrain to
read as a distant background; matching that visual cue here.

Implementation reuses the Vulkan renderer's background pipeline (same
shader, same descriptor set layout, straight-alpha blend already on).
New public surface on `VulkanRenderer`: `setSkyFromRgba` /
`setSkyWorldExtent` — one extra descriptor pool + draw call per camera,
nothing on the hot path.

Slow-scroll comes from extent math, not per-frame state: the sky quad
is sized `level extent × parallaxat` (centered on the same world origin
as the bg). Camera traversal of the level then walks only `1/parallaxat`
of the sky image's UV range — i.e. the sky scrolls `parallaxat×` slower
than the terrain, automatically. Jungle (parallaxat=2) → ½ speed,
Woods (parallaxat=4) → ¼ speed.

The sky is revealed through the level's Air pixels: at level-load,
`setupLevelGraphics` walks the attribute grid and punches alpha=0 in
the background texture wherever the cell is `Attribute::Air`. The
destruction painter is untouched — it writes alpha=255, so destroyed
terrain stays opaque (rock void), not transparent (sky). Synthetic /
procedural levels skip the sky path entirely (no `parallax.jpg`, no
levelDir, no Air punch).

`readParallaxAt(levelDir)` in `main.cpp` scans the imported
`config.txt` for `parallaxat = N`; defaults to 2 (the common case)
when missing or malformed; clamped to [1, 64].

Verified by smoke run on imported jungle (`parallaxat=2`, sky=1),
woods (`parallaxat=4`, sky=1), and `--gen` (sky=0). No new tests —
the API is graphical and exercised by the existing smoke binary.

### B2 — `.lev` `/KEY value` blob decoder + section3 round-2 RE (2026-05-31)
Reverse-engineered the per-level game-design parameters embedded in
the `.lev` container. Layout fully mapped at offset 0x122..0x1B8 (see
`TOU_RE.md` § "section3 — partial decode" for the byte-level table)
and validated bit-identically against all 4 shipped levels'
`makelev/<Stem>.txt` sidecars. Parser landed as a hermetic header
(`LevConfig.hpp`) + parser wired into `tou2d_import_lev`; the
generated `config.txt` now carries `waterc`, `gravity`,
`resistance`, `colldamage`, `bouncing`, `ambient`, `parallaxat`,
`gglevel`, `ggtheme`, `ggshape`, `repair`, `stuffd`, `signd`,
`randomseed` — no more hand-editing for original-physics fidelity.

`section3.bin` further explored: (value, count) pair format
confirmed, `(0xFF, 0xFF)` terminator confirmed, `(v, 0)` compact
short-form for single cells confirmed, high-nibble subtype + low-
nibble class taxonomy mapped. Value `0x03` = Air confirmed via
top-row alignment with `Jungle.tga`. Decoder still parked at
~63% structural match; the JPEG fallback handles gameplay so
this is post-v1 polish.

20-byte record payload (12-byte tail beyond x,y) examined across
all 4 levels: payload bytes 5..11 are always zero; bytes 0..4 vary
in a small enumeration that looks like a (team, kind, sub-kind)
tuple. All record positions verified to land on Air pixels — they
are spawn points / POIs.

Test: `tou2d_lev_config_test` (synthetic-bytes parser pin across
shipped-byte values, theme-string NUL semantics, randomSeed
endianness, buffer-too-short rejection). Verified the synthetic
encoder reproduces jungle.lev's `[0x122, 0x1B8)` bytes with zero
diffs. ctest 161/161 green.

### B1 — imported-level menu picker + JPEG-derived attribute fallback (2026-05-31)
Closes the "menu has no directory enumerator" gap on the MatchSetup
screen. `LevelEnumerator` scans `<assets>/levels/*` for valid imported
level dirs (`attribute.tga` OR `visual.jpg` present), sorted by name.
`MatchSetup` gains an `importedLevelIdx: u8` field (0xFF = synthetic
fallback) consuming one of the existing `_pad` bytes — POD size
unchanged. New `MatchSetupKnob::ImportedLevel` row sits at MatchSetup
index 5 between `UseGen` and `GenSeed`; empty enumeration formats as
`"(no levels)"` and cycling is a no-op. Host (main.cpp) enumerates at
startup, passes names to `UISystem::setImportedLevels`, and resolves
picked-idx → path at `restartMatch` time before `setLevelDir`.

`tou2d_import_lev` learned a JPEG-derived `attribute.tga` fallback —
when no sibling `makelev/<Stem>.tga` exists (the case for desert /
minibase / woods on a vanilla TOU install) it decodes `visual.jpg` and
emits a binary Air/Solid TGA via luminance threshold (BT.601, Y ≥ 64).
All 4 shipped `.lev`s now produce a loadable `attribute.tga` after
import. `LevelLoader` learned the same fallback in-process for drop-in
levels that ship only `visual.jpg`.

`section3.bin` (the original RLE attribute map) further reverse-
engineered — header layout (u32 record_count + 20-byte entity records)
confirmed across all 4 levels; RLE stream confirmed at 2× downsample of
the visual JPG resolution; full decoder parked at a 50% structural-
match milestone in favor of shipping the JPEG fallback. Findings
pinned in `TOU_RE.md` § 3 + § 6.

`assets/*` + `examples/*/assets/` covered by `.gitignore` — imported
levels are drop-in only, no redistribution from our side.

Tests: `tou2d_level_picker_test` (LevelEnumerator sort+filter, UISystem
Level scroller domain across empty / populated / shrink-revalidate
states, LevelLoader JPEG fallback grid build, reject-on-empty-dir);
existing `tou2d_match_setup_test` + `tou2d_player_setup_test` updated
to the 14-row layout. ctest 160/160 green.

## M7 — Polish pass + playtest debt (COMPLETE)

### M7.7 — Acceptance closeout (2026-05-31)
Documentation-only sweep. Every M7.1–M7.6 acceptance criterion flipped to
`[x]` with a pointer to the test that pins it (or M6.10-style "no automated
gate; playtest-driven" rationale for visual-character criteria). Outstanding
deferred work surveyed inline (engine-restart primitive, M7.5 kit spawning,
M7.6 procedural water sprinkle, M6.5 sub-screen apply gaps, M6.7b HUD
queue, M6.6 scoreboard depth, M5.1/M5.8 post-v1 items). ctest 159/159
green; 200-tick smoke bit-identical to M7.6. Commit `3aa056a`.

### M7.6 — Water mechanic (2026-05-31)
`Attribute::Water = 4` (stable enum byte, past `RepairBase = 3`).
`TerrainGrid::setWater(cx, cy)` round-trips: attr = Water, hp = 0
(non-blocking; bullets pass through). `MovementSystem` integrates buoyancy
(gravity scaled by `1 - wetness * kWaterBuoyancyFraction`) and extra drag
(`v *= exp(-kWaterDragPerSecond * wetness * dt)`); both produce smooth
half-wet blends. Test: `tou2d_water_test`. Commit `45ca5b1`.

### M7.5 — Pickup framework split (repair base vs kit) (2026-05-30)
`PickupKind` enum + `kPickupSpecs` catalogue separate the per-tick
`RepairBase` regen path (`RepairPickupSystem`) from a future one-shot kit
pickup path (`RepairKitSystem`). Framework lands; demo currently spawns
zero kits — extensibility surface is one row in the spec table + one
switch arm. Tests: `tou2d_pickup_framework_test`,
`tou2d_repair_pickup_test`. Commit `62ff12b`.

### M7.4 — Faction-aware allied AI (2026-05-30)
`LocalPlayer::factionId` (default sentinel = "no team"). Bot fire decision
runs `botShotHitsAlly(self, candidates)` — same-faction targets in range
and in arc suppress fire. Cross-faction or no-faction shots fire normally.
Test: `tou2d_faction_test` (8 scenarios), `tou2d_player_setup_test`
faction-cycle block. Commit covers M7.4.a–c.

### M7.3 — Thruster + damage smoke (2026-05-30)
`ParticleSystem::thrustColorForAge` lerps per-channel `kThrustColorHot →
kThrustColorCool` over `kThrustMaxAgeTicks`. Damage smoke gated by
`damageSmokeInterval` derived from HP fraction (band `[3, 32]` ticks).
`buildRenderFrame` round-trip pinned: `colorRGBA & 0x00FFFFFF` matches
the expected hot/cool endpoints. Test: `tou2d_particles_test`.

### M7.2 — Per-camera HUD ownership (2026-05-30)
**Engine surface change.** `DebugLine` and `DebugPoint` carry a public
`std::uint32_t cameraMask = 0xFFFFFFFFu` (default = visible from every
camera). Vulkan renderer groups debug vertices per camera index in
`recordFrame`; `recordCamera` draws only its own slice. `HudSystem`
per-slot primitives carry `cameraMask = (1u << slot)` so each viewport
renders only its own HUD. Winner banner keeps the all-ones default.
Test: `debug_camera_mask_test`. Backwards compatible — pre-M7.2 callers
see no change.

### M7.1 — Bot behavior polish (2026-05-30)
Three changes from playtest. (a) **Guarded retreat**:
`findNearestRepairTile(grid, origin, radius)` scans within
`kBotRepairSearchRadiusWU = 240 wu`; bot only retreats when a repair tile
exists in radius — otherwise falls through to engage. (b) **Chaos fire
layer**: per-tick xorshift32 with `kBotChaosFireChancePerTick = 0.005f`
breaks the "catatonic turret" wander-mode impression. (c) **Wander stall
investigation**: confirmed unreachable in the synthetic arena
(`kWanderRange = 360 wu` > arena diagonal `~115 wu`); design call is
"playtest drives retuning." Test: `tou2d_bot_behavior_test`.

### M7 Batch A — Three playtest bugs (2026-05-30)
- **§3** Notification text invisible: `ToastRenderSystem` emits world-space
  `DebugLine` strips but Vulkan renderer doesn't draw `DebugText`. Host
  now paints toasts via `UiOverlayBitmap` + `FontAtlas`. Commit `9d703c5`.
- **§2** Results HUD leaked round-1 state into round-2. `TouGame::onSetup`
  resets `winnerSlot_` / `winnerKills_` / `roundEnded_` at the top to
  cover every initialize path. Commit `f7874d8`.
- **§7** Camera scrolled past level perimeter. `CameraSystem::setLevelRect`
  clamps follow target to `[min + halfExtent, max - halfExtent]` per axis
  using `effectiveOrthoHalfH() × viewportAspect()`. Commit `f7874d8`.

---

## M6 — Complete GUI / UX polish (COMPLETE)

### M6.10 — Flow polish + acceptance pass (2026-05-30)
Final M6 sweep. Universal `Esc` routing audit, accessibility-knob
forwarding verified end-to-end, every M6.1–M6.9 acceptance criterion
flipped to `[x]`. ctest 158/158 green.

### M6.9 — Debug / benchmark overlay (2026-05-30)
`DebugOverlaySystem` with `~` toggle. Shows FPS, EWMA step time, entity
count, `commitHash`, top-3 systems by `lastStepSeconds`, world seed,
trace-sink state. Test: `tou2d_debug_overlay_test`.

### M6.8 — Notification / dialog layer (2026-05-30)
Toast channel: typed events fan out into a per-slot stack with TTL +
fade-out. Pickup feed, kill feed, system messages. Render via
`ToastRenderSystem`. Test: `tou2d_toast_render_test`.

### M6.7 — HUD polish + accessibility application (2026-05-30)
Thickened HP bar (3 parallel `DebugLine` segments). Low-HP red pulse
(`hpFrac ≤ 0.25` → red fill, alpha sine `[96, 220]` over 20 ticks).
Top-of-viewport warning marker (`bigWarnings` doubles size). `hudScale`
slider applied end-to-end (every WU constant scales). Photosensitive
particle alpha cap (×0.4 in `buildRenderFrame`, render-side only —
`commitHash` unchanged). Tests: `tou2d_hud_accessibility_test`,
`tou2d_particle_photosensitive_test`.

### M6.6 — Results / scoreboard screen (2026-05-30)
Scoreboard renders kills + winner per slot. Rematch flow (Results →
host re-emits round) returns the host to Menu / Setup / Rematch. Test:
`tou2d_results_screen_test`.

### M6.5 — Options menu + persistence (2026-05-30)
`Settings` POD + `settings.dat` binary format (host-endian; Linux x86_64
scope). Five sub-screens: Video / Audio / Controls / Gameplay /
Accessibility / Benchmark. Live-apply on back-out via
`pendingSettingsSave` drain. Test: `tou2d_options_screen_test` +
`tou2d_settings_io_test`.

### M6.4 — Pause menu (2026-05-29)
`UISystem` opens `Pause` on `Esc`. Resume / Restart match / Quit to
menu. `Replay::onStep` skips paused ticks so recording stays clean.
Test: `tou2d_pause_test`.

### M6.3 — Ship / player slot assignment (2026-05-30)
Setup-screen tab for ship + color + identity tag per slot. Faction
plumbing for M7.4 starts here. Tests cover the row-table model.

### M6.2 — Match / level setup screen (2026-05-29)
Every gameplay knob the CLI exposes today, exposed graphically + a
benchmark presets row. `MatchSetup` POD round-trips through the menu →
host boundary. (Engine-restart-with-MatchSetup primitive deferred.)

### M6.1 — UI state machine + main menu (2026-05-29)
`UISystem` state machine: MainMenu / Setup / Pause / Options / Results.
Keyboard navigation, focus model, safe defaults. Continue button shows
when a saved match exists (placeholder until M7+ engine-restart lands).

### M6.0 — Engine prereqs (font + UI compositor + KeyMap) (2026-05-29)
TTF font baked via `stb_truetype.h` (drop-in swappable). UI compositor
at `RenderPass::Overlay`. `KeyMap` action indirection so M6.5 Controls
rebind has somewhere to bind to.

---

## M5 — Polish + replay (stretch) (COMPLETE)

### M5.8 — Final weapon batch + M5 closeout (2026-05-29)
Last special weapons land (10 specials + Dumbfire = 11 total). M5
acceptance: replay round-trips bit-identical across every special-weapon
variant + procedural generator + repair pickups. Self-effect weapons
(Teleporter, Brickwall) deferred to post-v1 by design.

### M5.7 — Repair pickups + extra weapons + camera zoom (2026-05-29)
Initial repair-pickup framework (later split in M7.5). Camera zoom
scales per-viewport in split-screen. Extra weapons round out the v1 set.

### M5.6 — New special weapon types (2026-05-29)
Hitscan cone (Shotgun), organic-waste spawner, troopers ground-unit
spawner, turbo-boost self-buff, collapser terrain-trigger, kicker impulse,
nuclear-barrel persistent triggerable entity. Each exercises a distinct
engine touch-path.

### M5.5 — Procedural-level generator (2026-05-28)
Generator consumes GG theme directories + a `ProceduralLevelConfig` POD
to produce a tile grid. Replay-safe via deterministic RNG seeded from
config. (Water-cell sprinkle deferred — `ProceduralLevelConfig` would
exceed its 8-byte replay-header reservation; lands when the replay
header version bumps.)

### M5.4 — Replay capture + playback (2026-05-28)
`Replay::onStep` captures per-tick input + `EngineStats::commitHash`.
Playback re-feeds the input stream and asserts the hash matches
frame-for-frame. Skips paused ticks. Pinning test: replay round-trip
across a M5.3 + M5.5 demo session.

### M5.3 — Particle FX polish (2026-05-28)
Explosion, debris, smoke families. Per-family palette + TTL curve.
`forEachChunk<Particle, Transform>` exercise at high entity count.

### M5.2 — Polish round 1 (2026-05-28)
Visual cleanup across HUD + camera + sprite sizing. Frame-rate stability
on the perf box.

### M5.1 — Configurable humans/bots + split-screen (2026-05-28)
**Player counts.** `--humans=N` (1..4) + `--bots=M` (0..63) flags, default
`1 + 3`. Hard ceilings via `kMaxHumans = 4`, `kMaxBots = 63`,
`kMaxPlayerSlots = 67`. Spawn ring radius scales with `sqrt(N)`.

**Multi-camera split-screen.** `CameraSystem` emits one `Camera` per
human via `RenderFrameBuilder::addCamera`, each with a normalized
`Camera::viewport` rect (1-up full-screen, 2-up left/right, 3-up TL/TR/BL
with BR black, 4-up 2×2). Bots never get cameras. Per-viewport aspect
ratio keeps pixels square at quarter-screen. Per-viewport HUD anchors to
each human's view top-left.

**Known limitation (closed in M7.2).** Pre-M7.2 the per-slot HUD was
world-space debug geometry without per-camera filtering, so close-combat
overlap leaked HUD elements across viewports. M7.2 added `cameraMask`
to `DebugLine` / `DebugPoint`; HudSystem now sets `(1u << slot)`.

---

## M4 — Bots + Tier 2 import + content expansion (COMPLETE)

### M4.8 — Runtime sprite rendering + audio (2026-05)
Sprite atlases (per-team color tinting) wired into the renderer.
miniaudio backend drains `AudioPlay` events. Original `sfx/*.wav` plays
directly via the sound bank lookup.

### M4.7d — Centering + color model (2026-05)
Sprite frames centered to the per-ship anchor; per-team color tint
applied at draw time (single palette, channel selection per team).

### M4.7c — `.SHP` body decoder (2026-05)
**Breakthrough.** `.SHP` layout:
```
file_size  = header_bytes + 32 * 3 * frame_w * frame_h
body_start = file_size - 32 * 3 * frame_w * frame_h
```
Each rotation frame = `frame_w * frame_h` pixels, 3 bytes per pixel,
interleaved triplet: `b0` = hull palette index, `b1` = edge/wing
highlight, `b2` = cockpit/center detail. Recommended composite:
`b2 over b0`. Verified visually across all 9 stock SHPs — TIE fighters,
X-wings, the Fly, Batman, Destroyer all unmistakable. Decoder lives in
`scripts/decode_sprite.py` (Python prototype) and the C++ importer.

### M4.7b — Extended-header decode (2026-05)
Per-ship `ParsedHeader` extracts frame dimensions, rotation count (24 in
all 9), and max-HP from the anchor pattern `WW 00 HH 00 18 20`. Display
names diverge from the manual (e.g. PERH = "Butterfly", not "Basic ship")
— the in-file `displayName` is authoritative.

### M4.7 — Palette load + body visualizer (2026-05)
`PalCol.hpp` + `TgaWriter.hpp` header-only. CLI gains `--palette` +
`--width N` options; writes `palette.tga` swatch + `body_strip.tga`
visualization. Speculative decoder — superseded by M4.7c.

### M4.6 — `tou2d_import_shp` CLI scaffold (2026-05)
Standalone CLI scaffolded with the basic header parse from M4.5.

### M4.1–M4.5 — Bots + match modes + HUD + sound (2026-05)
`BotAISystem` (~150 lines state machine: distance / aim / retreat / fire).
Fill empty slots to total ship count. Match modes: deathmatch,
last-standing. HUD: score per player, weapon icon, HP bar. Sound:
`AudioSystem` + miniaudio backend; drop `sfx/` + `music/` into `assets/`.

**Acceptance**: 1 human + 3 bots deathmatch on jungle with original
sounds + original ship sprites runs at fixed 60 Hz for ≥ 5 minutes with
no crashes / OOMs. Bounded 300-tick smoke landed the sprite + audio
layers; full interactive verification is the user's host responsibility.

---

## M1–M3 — Foundation (COMPLETE)

### M3 — Weapons + projectiles + damage + multiplayer
`WeaponFireSystem` + 10 v1 weapons. `ProjectileSystem`, `Particle` POD,
`BulletShipCollisionSystem`, `BulletTerrainSystem`. `TerrainDamageSystem`
mutates tile HP / attribute. 2–4 local players via input device routing
(P1 keyboard + P2-P4 same-keyboard with the original bindings — works
per `toudoc_controls.htm`). Respawn after death, kill scoring.

**Acceptance**: 2 humans deathmatch on jungle; terrain visibly deforms;
round ends on score limit.

### M2 — Terrain + destructible tiles + Tier 0/1 import
Tile-grid `TerrainBlock` with attribute byte + HP. Visual layer: single
screen-sized quad with the level's JPG texture. Collision against
attribute-map cells. `tou2d_import_lev` CLI (Tier 1) produces
`assets/levels/<name>/{visual.jpg, attribute.tga, config.txt}`. Loader
populates `TerrainBlock` entities at startup.

**Acceptance**: `tou2d_import_lev jungle.lev` produces a directory;
loading shows the original Jungle level's visuals + ship collides with
the right shapes.

### M1 — Engine + 2D renderer foundation
Picked Vulkan renderer extension over a parallel `threadmaxx_2d` lib —
sprite vertex layout + orthographic camera fit cleanly into the existing
pipeline. Sprite atlas resource type registered through `ResourceRegistry`.
Empty world, ship POD, `MovementSystem`, `GravitySystem`. Single player,
no weapons, no terrain. "Thrust around an empty screen."

**Acceptance**: a single ship visibly accelerates with arrow keys;
gravity pulls it down; closes cleanly.

---

## See also

- [`README.md`](README.md) — user-facing overview, controls, build.
- [`TOU_PLAN.md`](TOU_PLAN.md) — live plan (deferred / open / future).
- [`../../CHANGELOG.md`](../../CHANGELOG.md) — engine-side release notes.
