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

### 2.1 Engine-restart-with-MatchSetup primitive
Single primitive, four call sites. Unlocks:

- **M6.2 `StartMatch` apply** — today host-logs only.
- **M6.4 `RestartMatch` apply** — today host-logs only.
- **M6.6 `Rematch` apply** — today host-logs only.
- **M6.1 MainMenu "Continue" enablement** — currently always-disabled.

Shape: `Engine::restartWith(MatchSetup)` that tears down + re-builds the
world from the new config, preserving the renderer + asset registry.
TouGame-side: `setMatchSetup(...)` then `engine.restart()`. The four call
sites all already produce a `MatchSetup` POD and log it; they just need
the host call.

### 2.2 M7.5 kit spawning
Framework + behavior split landed in M7.5; the demo currently spawns
zero kits. The Pickup user component + `RepairKitSystem` are wired.
What's missing:

- A spawn-time placer (both synthetic-level and procedural-level paths).
- A render asset (sprite or DebugPoint glyph) distinguishing kit from
  base on the HUD.

### 2.3 M7.6 procedural water sprinkle (blocked)
Blocked on bumping `ProceduralLevelConfig` past its 8-byte replay-header
reservation. Once unblocked: add `waterCellCount` knob parallel to
`repairTileCount`, sprinkle Water cells in the generator. Lands when the
replay header version bumps (own batch — affects format compatibility).

**Smaller M7.6 polish items that don't share that blocker**:

- `BulletTerrainSystem` water splash particles (bullets currently fly
  through Water unchanged — treated as Air via `hp == 0`).
- Smooth-blend wetness piped into thruster plume audio (a future audio
  polish batch).

### 2.4 M6.5 sub-screens whose values persist but don't apply mid-run
All persist round-trip correctly via `settings.dat`; each one is one
read at engine-restart time inside `TouGame::setMatchSetup` once the
matching feature ships.

- Key rebinding UI (Controls sub-screen).
- Live music driver (music slider persists; no driver pipes it through).
- Video knobs: fullscreen, vsync, ui_scale, resolution.
- Gameplay knobs: damageScale, respawnDelayTicks, cameraMode.
- Benchmark sub-screen: trace-sink + scripted-skip toggles.

### 2.5 M6.7b HUD polish queue
Carried forward from the M6.7 split:

- Damage-tick flash on HP bar.
- Weapon icon sprites (currently glyph-only).
- Identity badge (depends on M6.3 tag plumb — already shipped, just
  needs the HUD anchor).
- Match timer countdown (depends on time-cap `RoundEnded` event).
- Low-ammo / ship-on-fire warning markers.

### 2.6 M6.6 scoreboard depth
Per-slot deaths / damage-dealt / damage-taken columns. Needs
`BulletShipCollisionSystem` to publish per-slot stats — currently it
only aggregates kills.

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
