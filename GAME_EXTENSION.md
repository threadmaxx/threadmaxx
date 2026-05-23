# GAME_EXTENSION.md — rpg_demo growth plan (post-v1.2)

This document plans how `examples/rpg_demo/` extends past the
v1.2 baseline. The animating principle is **dual-purpose
content**: every feature that lands in the demo is also a
correctness test or a benchmark of an engine subsystem. The
game grows. The library's evidence base grows with it. The
two stay coupled.

This is a planning doc, not a contract. Batches ship in the
order their engine evidence justifies. Bench-driven sequencing
beats date-driven sequencing.

## 1. Where we are after v1.2

`examples/rpg_demo/` ships:
- 60×60 flat terrain, one player + ~50 NPCs (Idle / Wander /
  Flee / Fight) + 100 pickups, ~10k entities at default scale
- 22 registered systems (movement, combat, AI, day/night,
  hud, save/load, quests, hot-reload, …)
- 14 headless tests under `tests/rpg_demo/` exercising the
  shipped game code (not re-implementing it inline)
- Skinned-mesh rendering via `examples/vulkan_renderer/`
  (2-bone procedural capsule)
- `--stress` mode spawns 100k+ entities; `bench/rpg_stress_bench`
  is the canonical engine gate

`tests/rpg_demo/*.cpp` are gameplay tests; `bench/rpg_stress_*`
are engine-perf benches against rpg-shaped workloads. Both
patterns work. This doc proposes extending both.

## 2. Naming + conventions

- New rpg_demo batches use the `Batch D<n>` series. The last
  shipped is `D7` (real assets + hot reload, 2026-05-16); the
  asset batches `9b.1` … `9b.4.c` ran in parallel under
  `Batch 9b.x` for the Vulkan renderer side. **This doc starts
  at `Batch D8`.**
- Every batch must specify: scope, **gameplay deliverable**,
  **engine evidence** (which subsystem it exercises and what
  bench/test it produces), and a verification gate.
- Engine extensions trigger only on profile evidence from a
  shipped batch. The order of operations is: ship game feature
  → measure → if engine path is hot or breaks, plan an engine
  batch → ship that → ship next game feature.
- Sibling libraries (`threadmaxx_simd`, `threadmaxx_navmesh`,
  …) follow the same rule: they get spun up only when a demo
  batch exposes a need that doesn't belong in core.

## 3. The dual-purpose principle

Each game feature is shipped with two artifacts that ride
alongside it:

1. **A headless test under `tests/rpg_demo/`** that exercises
   the feature's gameplay-correctness invariants (e.g. "rain
   particle count peaks at N then decays" — a property the
   game cares about and tests can pin).
2. **A bench under `bench/`** that exercises the engine path
   the feature stresses (e.g. `bench/particle_storm_bench`
   measures burst-spawn + chunk-migration cost). The bench
   uses the *same* code path as the game; the game becomes the
   bench's workload.

This means a regression in one surfaces as a regression in the
other. Players never see the bench output; engine maintainers
never have to maintain two parallel rpg-shaped workloads.

## 4. Short-term batches (D8 – D11)

These are sequenced for the next sprint. No new sibling
libraries; each batch sits on top of v1.2 engine functionality.

### Batch D8 — Larger + uneven terrain  ✅ landed 2026-05-22

**Gameplay deliverable shipped.** Replaced the single 60×60
flat ground cube with a `cellsPerSide × cellsPerSide` grid
of static tile entities whose heights follow a 4-octave fBm
height field. Stress mode spawns 65 536 tiles (256×256);
normal mode spawns 1 024 (32×32). Tile colors run a 3-stop
gradient (grass → rock → snow) based on relative height.
Player and NPCs Y-snap to the terrain each tick via the new
`TerrainAttachSystem`. `NPCBrainSystem` rejects Wander
targets whose `slopeAt` exceeds 0.35 (~19°) with up to 3
re-rolls before falling through. `PickupSystem` switched to
XZ-only distance check (the pre-D8 3D check would have
missed pickups whenever the player was on a hilltop above
them).

**Game-side artifacts that shipped.**
- `examples/rpg_demo/Heightmap.hpp` — header-only,
  deterministic seeded fBm + bilinear `heightAt` +
  central-difference `slopeAt`. Borrowed by
  `TerrainAttachSystem`, `NPCBrainSystem`,
  `AnimationSystem`'s bob baseline, and the bench.
- `TerrainPatch` UserComponent on terrain entities
  (`cellX`, `cellZ`); lives on the static terrain archetype
  so brain / combat queries skip the archetype on the
  chunk-mask test.
- `WorldState::heightmap` (`std::shared_ptr<const Heightmap>`)
  + `WorldState::terrainCellsPerSide`. Tests can pre-seed
  `terrainCellsPerSide` to keep boot fast.
- `TerrainAttachSystem` — iterates `Transform + Velocity`
  chunks, writes `position.y = heightAt(x, z) + scale.y/2`
  via `ctx.single` when it changes. Registered between
  `MovementSystem` and `AnimationSystem`.

**Test that shipped.** `tests/rpg_demo/test_terrain_lookup.cpp` —
heightmap determinism (same seed → byte-identical field),
cell-center exact match (`heightAt(cellCorner) ==
sampleCell(...)`), bilinear midpoint = corner average,
out-of-bounds clamp, slope-threshold reachability (23 cells
above threshold at the configured parameters). 13 rpg_demo
tests total, 113 tree-wide.

**Bench that shipped.** `bench/terrain_query_bench.cpp`
(opt-in, behind `THREADMAXX_BUILD_BENCHMARKS=ON`). Four
shapes at three scales: scalar `heightAt`, scalar `slopeAt`,
parallel `forEachChunk` `heightAt`, parallel `forEachChunk`
`slopeAt` — at 16k / 64k / 256k entities. CSV columns match
the standard §3.9 schema. Headline numbers at 256k entities:

| label                        | ns/query |
|------------------------------|---------:|
| `height_single_threaded`     |    12.91 |
| `slope_single_threaded`      |    41.61 |
| `height_forEachChunk` (×4w)  |     3.75 |
| `slope_forEachChunk`  (×4w)  |    10.69 |

The 3.4× parallel speedup confirms the engine path is the
right shape for this workload.

**What this did NOT need (compared to the plan).**
- No `SpatialHash` height-aware variant.
- No `threadmaxx_terrain` sibling library.
- No `RoadGraph` (deferred to D11 where it'll be more
  natural alongside NPC schedules).
- No engine-side code touched. v1.2's `forEachChunk` + chunk
  filtering + `WorldView` did everything D8 needed.

**Engine extension trigger.** **None fired.** The per-NPC
slope query in `NPCBrainSystem` (at most one re-roll per
NPC per ~1.5s) is sub-percent of `MovementSystem::update`'s
cost. Parallel-bench evidence at 256k entities is 3.4×
faster than serial — well under any threshold that would
have justified the `threadmaxx_terrain` spinup.

### Batch D9 — Particles  ✅ landed 2026-05-22

**Shipped.** Burst-spawn particle entities driven by combat /
death / pickup events. Each particle carries
`Transform + Velocity + CubeRender + Particle` (the new
UserComponent). Motion runs through the engine's existing
`MovementSystem` — particles inherit the standard chunk
storage and the value-only `setTransform` fast path — and
`ParticleSystem` destroys entries whose remaining lifetime
hit zero. `ParticleEmitterSystem` drains the typed
`DamageDealt` / `EntityDied` / `PickupCollected` channels
and burst-spawns 10 – 32 particles per event (rate × kind
tuned in `DemoTypes.hpp`).

**`Particle` is immutable after spawn.** The engine derives
remaining lifetime from `tick * dt - spawnTimeSeconds` at
read time rather than mutating the UC each tick, which
would migrate every particle entity twice per tick (out of
and back into the Particle chunk) and defeat the chunk-
stability premise the bench was meant to measure.
Documented in `DemoTypes.hpp`'s comment block above the
struct.

**`ParticleEmitter` reserved (not used yet).** Spec called
for a per-entity emitter UC; the actual emit path keys off
engine event channels rather than per-entity state. The UC
is still registered so the archetype layout is stable when
D10+ attaches per-NPC emitter knobs.

**Game-side artifacts shipped.**
- `examples/rpg_demo/DemoTypes.hpp`: `Particle`,
  `ParticleEmitter`, and `kParticles*` tuning constants.
- `examples/rpg_demo/ParticleSystem.{hpp,cpp}`: ages out
  expired particles. `skippable()` so it drops first under
  tick-budget pressure.
- `examples/rpg_demo/ParticleEmitterSystem.{hpp,cpp}`:
  drains last-tick events, reserves all handles in one
  `ctx.reserveHandles` call, batches the spawns into one
  `ctx.single` lambda.

**Test.** `tests/rpg_demo/test_particle_lifetime.cpp` —
spawn 100 particles with `initialLifetime = (i+1) *
0.05s`, advance the engine, verify exactly the expired
cohort is destroyed at the midpoint and all are gone past
the maximum lifetime. 14 rpg_demo tests; 114 tree-wide.

**Bench.** `bench/particle_storm_bench.cpp` (opt-in) — burst-
spawn + age-out workload at 1k / 5k / 25k / 100k
particles/sec. At 60 Hz that's per-tick `N/60` (round-half-
up). Two CSV rows per scale: full-step latency and commit-
only latency. Note column carries the final-tick
`commitHash` for determinism diffing.

**Engine evidence at 100k particles/sec (≈ 50 k live):**

```
step_mean    = 12.30 ms       (within 60 Hz 16.7 ms budget)
commit_mean  =  1.75 ms       (~14 % of step time)
throughput   = 135 753 particles/sec sustained
```

Commit time scales linearly with command volume (~530
ns/command at 3.3 k commits/tick), well within budget. The
batch B30 per-archetype hash rollup folds into the same
`commitDurationSeconds` slice without becoming a visible
cost. **Engine extension trigger does NOT fire.** No need
to spin up `FUTURE_WORK.md §5.3`'s transient-lifetime
component class; v1.2's existing user-component plumbing
sustains the workload.

**Items intentionally not in v1.2:** visible alpha fade (the
`Particle::fadeSeconds` field is reserved but not consumed
yet — adding it requires plumbing the UC id into
`CubeRenderSystem` for an alpha-scale multiply, future
polish). Footstep-dust particles (would require per-tick
emit from a velocity-aware emitter; deferred to D10's
weather particle work).

### Batch D9.5 — Voxel terrain pivot  ✅ landed 2026-05-22

**Why this exists.** Earlier rounds tried to make the heightmap-
derived terrain look "smooth" — bilinear `heightAt`, edge-warped
interpolation, slab-thickness tricks. They all introduced
secondary artifacts (tripping, beehive walls, ambiguous slope-
reject semantics) or required deeper renderer work the demo
doesn't need. Round 9 pivots to **discrete Minecraft-style
voxels**: every heightmap cell is one block tall; adjacent cells
form vertical walls; traversal allows 1-block step-ups and rejects
anything taller.

This collapses three open problems (sloped sides, smooth Y curve,
slope-reject thresholding) into one well-defined gameplay rule.

**Shipped.**
- `Heightmap::heightAt` is now a step function that quantizes the
  raw fBm sample to integer multiples of `kBlockUnit` (1.0 m).
  Every point inside a cell reads the same Y. Bilinear /
  edge-warp interpolation is gone.
- `Heightmap::blockUnit()` exposes the quantization step for
  game-side code that needs it (currently just
  `TerrainAttachSystem`'s step-up check).
- `TerrainAttachSystem` enforces a **1-block step-up cap**: when
  the entity's new XZ falls inside a cell whose quantized height
  exceeds its previous-tick ground height by more than
  `kStepUpMax` (1.0 + epsilon), revert XZ to the entity's last
  safe position. Airborne players bypass (jumping clears walls).
  Per-entity prev-XZ cached in `prevPos_` keyed by
  `EntityHandle::index` with a generation guard for slot reuse.
- The cosmetic Y-bob remains positive-only (round-3 contract)
  but no longer interacts with the heightmap's now-discrete Y —
  `baseY` snaps cleanly at every cell.

**Game-side artifacts shipped.**
- `examples/rpg_demo/DemoTypes.hpp`: `kStepUpMax` constant.
- `examples/rpg_demo/Heightmap.hpp`: voxel doc block + `blockUnit()`
  accessor + step-function `heightAt` + `quantize_` helper.
- `examples/rpg_demo/TerrainAttachSystem.{hpp,cpp}`: `prevPos_`
  cache + step-up rejection logic.

**Tests updated.** `tests/rpg_demo/test_terrain_lookup.cpp` was
re-shaped: cell-origin sampling → quantized exact match; "bilinear
midpoint" check replaced with "every point inside a cell reads the
same Y"; OOB clamp asserts equality (not relErr). Slope-reject
still finds cells > 0.35 because the central-differences gradient
of a quantized field still hits ≥ 0.5 at every 1-block boundary.

`tests/rpg_demo/test_animation.cpp` was relaxed: the
positive-only bob means `minY = base` always, so the assertion
shifted from "swings above AND below baseY" to "swings at all"
(`maxY - minY > 0.03`). The voxel pivot doesn't break the bob
itself — entities that hit a wall keep their velocity vector and
keep bobbing in place.

**Engine evidence.** None — `Heightmap` is pure header-only
demo-side code, and `TerrainAttachSystem` was already a normal
ISystem. No engine changes. The per-entity prev-XZ cache lives
in the system itself, not in storage; it's a small flat vector
keyed by EntityHandle::index, bounded by the high-water entity
count.

**What's deferred to D10+** (the voxel gameplay tranche; see
below).

### Batch D10 — Voxel block attributes + visual variety  ✅ landed 2026-05-23

**Gameplay deliverable.** Each terrain cube becomes a typed
**block** with attributes: `BlockKind` (Grass / Dirt / Stone /
Sand / Snow / Water / Wood), `hardness`, `walkable`, base color.
The terrain spawn loop in `DemoGame::onSetup` samples block kind
from the heightmap value (low → sand/water, mid → grass/dirt,
high → stone/snow). Color comes from the block kind rather than
the 3-stop heightmap-gradient hack. Non-walkable blocks (water)
get a different surface contract — entities sink to a "water
level" instead of being snapped to the top.

**Engine evidence.**
- First real exercise of a heterogeneous-archetype terrain world.
  Pre-D10: every terrain entity has the same component mask
  (`Transform + Faction + StaticTag + CubeRender + TerrainPatch`).
  Post-D10: each block kind keeps that mask but additionally
  carries a `BlockData` UserComponent with kind+attributes. All
  blocks share one archetype (they all carry `BlockData`), so
  archetype count is still small.
- Tests the chunk-iteration path's tag-filtering on a realistic
  ~1k-2k entity terrain workload (D8 already does this; D10
  adds the variety without changing the shape).

**Game-side artifacts.**
- `BlockKind` enum + `BlockData` UserComponent.
- A `kindAt(heightmap value, surface y) → BlockKind` mapping
  function in `Heightmap.hpp` or a sibling header.
- `BlockPaletteSystem` (one-shot at init?): assigns colors at
  spawn time — no per-tick cost.

**Test.** `tests/rpg_demo/test_block_palette.cpp` — verify the
spawn-time kind distribution matches the heightmap height ranges
(e.g. height < -2 m → Sand; height ≥ 9 m → Snow). Pure
deterministic-data test.

**Bench.** None — block-attribute work is one-shot at boot.

**Engine extension trigger.** None expected.

### Batch D11 — Harvestable blocks  ✅ landed 2026-05-23

**Gameplay deliverable.** Player can break (left-click) and place
(right-click) blocks. Breaking a block: the entity becomes a
`DroppedItem` (small spinning cube of the block's color) for ~30
seconds, then despawns. Pickup hooks into the existing
PickupSystem path. Placing: spend a `DroppedItem` from inventory
to spawn a new block at the cell the player is targeting.

**Engine evidence.**
- Tests the spawn/destroy churn path under realistic mid-tick
  rates (~5-20 break/place events per minute). The B30
  per-archetype hash rollup should handle this trivially since
  the involved archetypes are small.
- First test of `Engine::events<BlockBroken>()` /
  `BlockPlaced` carrying enough info (cell coords, kind,
  player) for downstream systems.
- Tests `AssetReloaded` for the player's inventory UI — when a
  block kind's color asset changes, the inventory icon updates.

**Game-side artifacts.**
- `Inventory` UserComponent on the player (small fixed-size
  array of `{BlockKind, count}` stacks).
- `BlockEditSystem` (new ISystem): drains `BlockBroken` /
  `BlockPlaced` events from input, applies the change via
  `cb.destroyEntity` / `cb.spawn`.
- New typed events: `BlockBroken{cellX, cellZ, kind, breaker}`
  and `BlockPlaced{cellX, cellZ, kind, placer}`.
- `DroppedItem` UserComponent + matching cosmetic spawn logic.

**Test.** `tests/rpg_demo/test_block_harvest.cpp` — spawn a 4×4
terrain patch, break the center block via a synthetic
`BlockBroken` event, verify the entity is destroyed in the next
commit AND a `DroppedItem` appears at the cell. Place it elsewhere
via `BlockPlaced`, verify the new entity exists with the right
kind.

**Bench.** `bench/block_edit_bench.cpp` — sustained edit rate
(break + place pairs at 100 / 1k / 10k ops/sec) measured against
`commit_mean` and per-frame variance. Stresses the spawn/destroy
churn the B30 hash rollup is supposed to absorb cheaply.

**Engine extension trigger.** If `commit_mean` at 10k ops/sec
exceeds ~3 ms, the engine probably needs faster spawn paths
(currently every spawn goes through `setMaskAndMigrate`). Most
likely candidate: a bulk-spawn API that pre-allocates N slots
in one chunk before any commits.

### Batch D12 — Voxel chunking (renderer-side)  ✅ landed 2026-05-23

**Shipped scope.** Phase 1 of the voxel-chunking pivot: the world
grew 4× in area and ~67% taller, the renderer learned a
normal-mode distance cull, and each terrain block now carries a
`TerrainChunk` UC pinning it to a 16×16-cell chunk group. The
chunk membership is the foundation for future per-chunk rebake
(greedy meshing into a single DrawItem); for now it's a cheap
denormalized index that lets diagnostics + tests group blocks by
chunk without re-deriving from `(cellX, cellZ) /
kTerrainChunkSize` on every iteration.

**What changed.**
- `kTerrainExtent` 48 → 96 m, `kHeightmapResolution` 48 → 96,
  `kNormal/StressTerrainCellsPerSide` 48 → 96. The world covers
  4× the XZ area; the demo now spawns ~92 000 voxel blocks (up
  from ~14 000).
- `Heightmap` `kHeightScale` 12 → 20 m. Peaks now rise to ~10×
  player height (was ~6×) and produce taller stacked silhouettes.
- `kindAt` palette thresholds rescaled (2/6/9 → 3/10/16) so the
  Sand/Grass/Stone/Snow distribution stays roughly proportional
  to the new 0–20 m range.
- `kTerrainChunkSize = 16` — terrain blocks are grouped into
  16 × 16 cell chunks. `TerrainChunk{chunkX, chunkZ}` UC
  attaches at spawn AND on every D11 place.
- `CubeRenderSystem` distance-culls in BOTH normal and stress
  modes (was stress-only). Normal mode radius = 36 m; stress
  stays at 12 m. Without this the bigger world's 92k blocks
  would jam the per-tick instance buffer build.
- `DemoTestHarness::makeHeadless(cellsPerSide=16)` overrides
  the demo's terrain cell count so rpg_demo tests don't pay 6×
  boot time. Tests that exercise terrain math
  (`test_block_palette`, `test_block_harvest`) pass an explicit
  larger value.
- `test_animation` Y-range upper bound loosened from 0.30 →
  2.0 to accommodate the deeper heightmap (the NPC walks across
  more 1-block step-ups in 90 ticks now).
- `test_block_palette` palette breakpoints rebased for the new
  thresholds.

**Deferred to a later batch (still on the table).** Per-chunk
mesh rebake into a single DrawItem; greedy meshing; background
rebuilder using `snapshotAsync`. The current phase 1 doesn't
need them — distance culling keeps the instance buffer build
within the per-tick budget for the 96 m world. Profile data
will tell us if Phase 2 is worth doing.

**Test coverage.** Full engine + rpg_demo suite green at 116/116
on both `build/` (Release) and `build-werror/` (strict) trees.

### Batch D10 (deferred) — Weather

**Gameplay deliverable.** Rain (particle burst from above
camera, fades on ground contact), fog (camera-relative
distance falloff in the shader), wind (vector field affects
particles + grass-tuft sway). Weather state transitions
over 5-minute cycles; AI sight radius drops during fog.

**Engine evidence.**
- Global state propagation. The weather state lives in
  `WorldState`; multiple systems read it (`ParticleEmitterSystem`,
  `NPCBrainSystem`, the shader pipeline via uniform). Tests
  the engine's reads-shared-state-without-conflict pattern
  (no `Component` bit involved — it's free-floating).
- Tests `EventChannel<WeatherChanged>` cross-system messaging
  at human-time granularity (one event per ~30 seconds).
- Tests `ResourceRegistry::addRefCounted` hot-swap of
  weather-preset textures.

**Game-side artifacts.**
- `WeatherKind` enum + `WeatherState` in `WorldState`.
- `WeatherSystem` (new ISystem): transitions kinds on
  schedule, emits `WeatherChanged` on transition, owns the
  RNG for weather variation.
- `EnvironmentVisibility` (read by `NPCBrainSystem`'s
  sight-cone test).

**Tests.** `tests/rpg_demo/test_weather_transitions.cpp` —
seed RNG, run 600 ticks, verify the transition log matches
the seeded sequence (determinism), verify `WeatherChanged`
events fire exactly at transitions.

**Engine extension trigger:** none expected.
`EventChannel<WeatherChanged>` is a cold channel; this is
gameplay logic on top of v1.2 primitives.

### Batch D11 — NPC daily routines + schedules

**Gameplay deliverable.** Each NPC carries a 24-hour
schedule (`Sleep` → `Eat` → `Work` → `Eat` → `Socialize` →
`Sleep`). Day/night time drives the next-task selection.
NPCs walk to named locations (using D8's road graph) and
linger until the schedule advances. Hostile NPCs override
this with the existing Fight/Flee logic.

**Engine evidence.**
- Tests `TaskTag` dependencies. `ScheduleSystem.provides({
  "schedule.advanced" })`; `NPCBrainSystem.dependencies({
  "schedule.advanced" })` — the topo-sort guarantees Brain
  reads the just-published schedule decision in the same
  tick. First non-rpg_demo test of the §3.4 task-graph
  feature on a realistic chain.
- Tests deterministic replay with `SkipPolicy::Scripted`.
  Record a 1000-tick schedule trace; replay it on a second
  engine instance; verify schedule decisions byte-identical
  (this is the v1.2 hash contract in action on game-side
  state).
- New bench: `bench/schedule_brain_bench` — 100k NPCs with
  schedules, measures the cross-system handoff overhead +
  whether the brain's read-after-schedule barrier dominates.

**Game-side artifacts.**
- `Schedule` UserComponent: array of `(timeOfDayStart,
  taskKind, locationId)` tuples.
- `NpcState` extension: `RoutineTask currentTask` (new
  enum) and `targetLocation` (uint32_t into D8's road
  graph).
- `ScheduleSystem`: writes the day's task each tick from
  the schedule + current time, emits `ScheduleAdvanced`
  event on task transition.

**Tests.** `tests/rpg_demo/test_schedule_routine.cpp` — 24
ticks (compressed day), verify each NPC transitions through
its scheduled tasks in order; verify a hostile-NPC override
correctly preempts the schedule.

**Engine extension trigger:** the schedule + brain handoff
is the first multi-tick deterministic-replay test we have
that goes beyond the synthetic `concurrency_soak_long`. If
replay diverges, that's a v1.2 hash contract bug; the
existing `archetype_hash_determinism_test` doesn't cover
this exact case.

## 5. Mid-term batches (D12 – D13)

Larger structural additions. Each justifies a corresponding
engine bench at minimum, possibly an engine extension.

### Batch D12 — More NPC types + faction interaction

**Gameplay deliverable.** Three new NPC archetypes —
Merchant (stationary unless attacked, sells trinkets to
player), Guard (patrols city blocks from D13, attacks
hostile NPCs on sight), Wildlife (deer / wolf, behave
independently of faction; wolves are hostile to deer AND
to the player at night). 3-4× the current NPC count at
default scale (~200 NPCs).

**Engine evidence.**
- Archetype-count growth. Each NPC type carries a different
  set of UserComponents → distinct archetype masks. Current
  demo runs ~6 archetypes; D12 pushes that to ~10-12. Tests
  whether the per-archetype hash rollup (§3.6 batch 30)
  scales linearly with archetype count.
- New bench: `bench/archetype_diversity_bench` — synthetic
  N-archetype workloads from 5 to 50 archetypes, measuring
  B30's `finalizeCommitHash` parallel-recompute scaling.
- Tests `ComponentSet`'s 48 spare bits (currently 16 in
  use). D12 may force one or two new game-side
  UserComponents past the bit-16 floor; verifies the
  registry assigns them deterministically.

**Game-side artifacts.**
- New UserComponents: `Merchant`, `Guard`, `Wildlife`.
- `FactionRelationsTable` in `WorldState`.
- `WildlifeBrainSystem` (separate from `NPCBrainSystem` to
  keep the human-NPC AI clean).

**Tests.** `tests/rpg_demo/test_faction_attack.cpp` —
spawn a hostile NPC near a guard, verify the guard engages;
spawn a wolf near a deer at night, verify the wolf chases.

**Engine extension trigger:** if `archetype_diversity_bench`
shows B30's finalize getting slow at 30+ archetypes, the
parallel-recompute step needs work-stealing across chunks
instead of one-job-per-chunk. Documented as a possible
B30-follow-on in `OPTIMIZATION_PATH.md`.

### Batch D13 — Cities + dense static geometry

**Gameplay deliverable.** Two named towns on the terrain.
Each has ~50 buildings (cubes + pyramidal roofs from D7's
OBJ pipeline), roads connecting them, scattered props (carts,
crates, lampposts). Most entities have `StaticTag` — they
don't move and shouldn't pay per-tick `MovementSystem`
overhead.

**Engine evidence.**
- Tests `StaticTag` chunk-skip path under realistic load.
  Pre-D13: maybe 50 static entities total. Post-D13: ~1000
  static building/prop entities. This is the test of
  whether `MovementSystem` correctly compiles down to
  chunk-level skips (i.e. visits zero rows in the static
  chunk).
- Tests `cullByFrustum` 32-camera mask cap at realistic
  per-camera entity counts. Current visibility test sees
  ~150 entities; D13 pushes it to ~2000. The 32-camera
  shadow-cascade scenarios (lights pretending to be cameras)
  finally have enough entities to matter.
- Tests B30 hash rollup on archetype shapes whose chunks
  never get touched (static entities). The cachedHash should
  never get re-computed on those chunks after initial spawn
  — directly observable via `EngineImpl::dirtyChunkCount`
  metric (already tracked in JobSystemStats? — actually no,
  expose via a new diagnostic if needed for the bench).
- New bench: `bench/static_culling_bench` — 256 → 2048 static
  cubes × 1/4/16/32 cameras, measures the cullByFrustum
  cost AND verifies the static-chunk skip works (zero
  per-tick mutations on static chunks).

**Game-side artifacts.**
- `Building` / `Prop` UserComponents.
- `CityLayoutSystem` (preStep, one-shot at init): spawns
  the buildings + roads from a procedural layout seeded by
  city-id.

**Tests.** `tests/rpg_demo/test_city_render.cpp` — initialize
a city, run 60 ticks, verify the building entity count stays
constant (no spurious destroy/respawn), verify their
`Transform`s never change.

**Engine extension trigger:** if `static_culling_bench` at
2048 cubes × 16 cameras shows visibility-culling becoming
hot, the engine likely needs a hierarchical culling step —
e.g. a coarse-grid pre-pass that buckets entities by cell
before the per-AABB cull. **This is a candidate v1.3 batch.**

## 6. Long-term batches (D14+) — gated on real evidence

These are documented for context; they do not ship until a
shipped batch produces profile evidence justifying them.
Each likely brings in a sibling library.

### Batch D14 — Pathfinding / navmesh

**Trigger.** D11 + D13 mean NPCs follow schedules between
city locations. The current heuristic (drift toward target,
slope-reject steep cells) will fail on city geometry — NPCs
need to route around buildings, not into them.

**Likely scope.**
- Sibling library: **`threadmaxx_navmesh`**. The engine
  already exposes `NavAgentRef` (§3.1 batch 5) as a hook;
  this batch implements the producer side.
- `NavmeshBuilder` (game-side at first): bakes a 2.5D
  navmesh from the height-field + the static building
  AABBs at boot.
- A* or NavMeshRequest queue inside the sibling library;
  the engine just plumbs a per-tick "advance pathfinding"
  call.

**Why a sibling library:** routing math is computationally
heavy and changes independently of the engine's lifecycle.
The engine's job is to expose the hook (already done) and
to keep the determinism contract (a navmesh query must
produce the same path bit-for-bit given the same input).

### Batch D15 — Audio events

**Trigger.** D9 (particles) and D12 (more NPC types)
introduce ~dozen new event channels (`SwordHit`,
`SwordSwing`, `MerchantGreet`, `WolfHowl`, …). Each is a
natural audio cue; today they're consumed only by
`HudSystem` for log lines.

**Likely scope.**
- Sibling library: **`threadmaxx_audio`** (probably
  miniaudio or OpenAL-soft under the hood).
- A `ListenerComponent` carried by the player.
- An `AudioCue` POD published on a typed event channel.
- The sibling library subscribes to `events<AudioCue>()`
  and mixes them on its own thread.

**Why a sibling library:** the engine doesn't own audio
output, and audio mixing has latency requirements that
don't fit the fixed-step sim model.

### Batch D16 — Animation graph

**Trigger.** D6's procedural 2-bone capsule was a proof of
concept. Real characters need richer skeletons, blended
clips, and IK on at least one chain (NPCs grabbing
trinkets, looking at the player). The current
`AnimationStateRef` slot in `Components.hpp` is a
placeholder — D16 is when its semantics get fleshed out.

**Likely scope.**
- Sibling library: **`threadmaxx_anim`**. Owns clip data,
  blending math, IK solvers.
- `AnimationStateRef` becomes a real ResourceId pointing
  into the sibling library's animation state cache.
- Per-NPC animation update happens off the engine's main
  pipeline (the animation library has its own job pool;
  the engine reads the resolved pose per tick).

**Why a sibling library:** animation math (especially IK)
benefits from SIMD that's tighter than the engine's general
job dispatch. Likely consumes `threadmaxx_simd`.

### Batch D17 — Physics

**Trigger.** D9 particles look fine without physics.
Sword-swing knockback, NPC ragdolls, deer-stumble-on-arrow
need real collision + dynamics.

**Likely scope.**
- Sibling library: **`threadmaxx_physics`** (probably wraps
  PhysX or Jolt).
- `PhysicsBodyRef` slot (already in `Components.hpp`)
  becomes the real handle into the physics library's body
  cache.
- Engine plumbing: a per-tick "step physics" call between
  `MovementSystem` and `HierarchySystem`.

**Why a sibling library:** physics solvers are huge,
opinionated, and have their own determinism stories. The
engine ships hooks; the solver is someone else's library
problem.

### Batch D18 — GPU-driven particles (renderer-side)

**Trigger.** If D9's CPU-side particle system at 100k
particles/sec saturates the commit phase, the realistic fix
isn't to optimize the engine path — it's to move particles
off entity storage entirely. GPU compute shaders integrate
per-particle position; the engine just publishes spawn
events.

**Likely scope.**
- `examples/vulkan_renderer/` gains a particle pass +
  compute shader.
- The engine ships a `ParticleSystemHandle` (game-side
  UserComponent pointing into the renderer's particle
  state cache).
- `EventChannel<ParticleSpawn>` becomes the engine→renderer
  cue.

**Why this is a renderer batch, not an engine batch:** the
engine doesn't own the GPU. Engine's contribution is the
event channel and the renderer-side hook pattern (the same
pattern D9's CPU particles use, just routed differently).

## 7. Sibling library landscape (post-D17)

If every D14-D17 trigger fires, threadmaxx ends up sitting
inside this constellation:

```
                ┌──────────────────┐
                │   threadmaxx     │  (core)
                │   v1.2 + Dx      │
                └──────────────────┘
                  │   │   │   │   │
       ┌──────────┘   │   │   │   └──────────┐
       │              │   │   │              │
┌──────────────┐ ┌────────┐ ┌────────┐ ┌─────────────┐
│ threadmaxx   │ │ navmesh│ │ audio  │ │ anim        │
│ _simd        │ │        │ │        │ │             │
│ (shipped)    │ │ (D14)  │ │ (D15)  │ │ (D16)       │
└──────────────┘ └────────┘ └────────┘ └─────────────┘
                                              │
                                       ┌──────────────┐
                                       │ physics      │
                                       │ (D17)        │
                                       └──────────────┘
```

Each sibling has its own semver, its own CHANGELOG, and its
own decision about whether to consume `threadmaxx_simd`. The
engine NEVER takes a hard dep on a sibling — they're all
opt-in via the existing `ResourceRegistry` +
`IResourceLoader` + `EventChannel` plumbing.

## 8. Engine optimization opportunities surfaced by this plan

Speculative — each fires only if its trigger batch produces
the evidence:

- **D8 height-field query**: possible `SpatialHash` height-
  aware variant, OR `threadmaxx_terrain` sibling.
- **D9 particle burst**: possible "transient lifetime"
  component class (per FUTURE_WORK §5.3); first real-world
  driver to revisit that deferred work.
- **D11 deterministic replay**: more demanding test of the
  v1.2 hash contract than anything in the current test
  suite; if it diverges, that's a contract bug.
- **D12 archetype diversity**: forces B30's finalizeCommitHash
  to scale to 10+ archetypes; if linear scaling fails, the
  parallel-recompute step needs work-stealing across chunks.
- **D13 static visibility**: if `cullByFrustum` at 2k AABBs
  becomes hot, the engine needs hierarchical culling. **Most
  likely v1.3-blocking item from this plan.**

## 9. Shipping bar (per batch)

Each batch must produce, in this order:

1. The gameplay feature, playable / observable in the demo.
2. The headless test under `tests/rpg_demo/` (gameplay-side
   correctness).
3. The bench under `bench/` (engine-side perf, sharing the
   batch's workload code).
4. Verification: ctest 100% on `build/` and `build-werror/`;
   sanitizer trees green; the new bench produces a CSV the
   PR body references.
5. CHANGELOG entry under `## [Unreleased]` (or the next
   minor's section if release is near).
6. Doc updates: `examples/rpg_demo/README.md` (if exists) +
   `FUTURE_WORK.md §3.11` updated with the as-shipped
   writeup.

If any one of (1)-(4) is missing, the batch is not done. (5)-(6)
are part of the same commit, not a follow-up.

## 10. What this plan deliberately does NOT cover

- **Engine internals refactors.** This doc is about growing
  the game and surfacing engine work via that growth. If you
  want to refactor `ArchetypeChunk` or rewrite the
  scheduler, that's `OPTIMIZATION_PATH.md` territory, not
  here.
- **Renderer-internal work.** Visual fidelity (PBR
  materials, shadows, anti-aliasing) lives in
  `examples/vulkan_renderer/`'s own roadmap. This doc
  triggers renderer work only via concrete game needs
  (D9 particles → D18 GPU particles).
- **Tooling and editor UI.** Out of scope until v2.x per
  `OPTIMIZATION_PATH.md §5`.
- **A specific game beyond "rpg_demo grows".** The demo
  stays a *demo* — its purpose is to exercise engine paths,
  not to become a shippable game.

## 11. Definition of done for the D8-D11 short-term tranche

When all four short-term batches have landed:

- ✅ Terrain is 256×256 with elevation; player and NPCs
  navigate it correctly.
- ✅ Particles spawn from combat / movement events; demo
  has visible visual feedback for player actions.
- ✅ Weather transitions across a session; fog visibly
  affects AI sight; rain particles spawn.
- ✅ NPCs follow daily schedules; the city is populated
  through the day, sleepy at night.
- ✅ Four new benches (`terrain_query_bench`,
  `particle_storm_bench`, `schedule_brain_bench`,
  `weather_transition_bench`) are in the
  `THREADMAXX_BENCHMARKS` list, with baseline CSVs in the
  PR bodies that landed them.
- ✅ Four new tests (`test_terrain_lookup`,
  `test_particle_lifetime`, `test_weather_transitions`,
  `test_schedule_routine`) are in ctest, passing on all
  four build trees (Release, Werror, TSAN, ASAN+UBSAN).
- ✅ The engine has shipped zero `src/` changes for the
  short-term tranche, OR has shipped exactly one engine
  batch closing a profile-evidence-driven optimization.

The short-term tranche is the test: can we extend the demo
materially without touching the engine? If yes, v1.2's
abstractions are right-sized. If no, the engine extension
the demo forced becomes the next OPTIMIZATION_PATH batch.

---

**Last updated.** 2026-05-22 — D9 landed, then D9.5 (voxel
terrain pivot) landed as a follow-on. The original D10 (Weather)
is deferred behind the voxel tranche (D10-D12 now cover block
attributes, harvestable blocks, and renderer-side chunking).
Weather will likely re-emerge as D13 or D14 once the voxel work
settles.

Refresh this doc when the next batch lands (replace its planning
block with the as-shipped writeup) or when a long-term batch
trigger fires.
