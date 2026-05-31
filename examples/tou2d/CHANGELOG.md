# tou2d CHANGELOG

Chronological per-batch landing log. Each entry: ship date, scope, the
test that pins the contract. Deep design context (rationale, alternatives
considered, empirical numbers) lives in commit messages and in the
pre-distillation history of `TOU_PLAN.md`.

For the live plan (deferred / open / future work), see
[`TOU_PLAN.md`](TOU_PLAN.md).
For the user-facing overview, see [`README.md`](README.md).

---

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
