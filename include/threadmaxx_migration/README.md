# `threadmaxx_migration`

Save-file versioning, schema evolution, and offline conversion for
projects built on the `threadmaxx` engine.

**Status**: v1.0.0 — production-ready. The M1–M8 batch sequence
closed; 30 headless tests pin the contract.

## What

The core engine ships **serialization hooks** (`WorldSnapshot` plus
per-component `serialize`/`deserialize` traits). It deliberately does
**not** ship a versioned migration system — that's a sibling concern,
and this is the sibling.

The library is **data-first**: it transforms serialized records
(typed binary fields read from a save file), not live engine state.
The same code path serves runtime "load an old save" and offline
"convert this batch of saves to the new schema" use cases.

Two execution surfaces share one public API:

- **In-game runtime path** — link `threadmaxx::migration` into your
  game; load a `RecordSet`, run the registered pipeline, hand the
  upgraded snapshot to the engine via `cb.spawn(...)`.
- **Offline converter tool** — the `threadmaxx_migration_convert`
  executable (under `tools/migration_convert/`) drives the same
  pipeline against on-disk save directories for batch upgrades.

## Quick start

```cmake
target_link_libraries(my_game PRIVATE threadmaxx::migration)
```

```cpp
#include <threadmaxx_migration/threadmaxx_migration.hpp>

namespace mig = threadmaxx::migration;

// One-time at startup: register a v1.2 -> v1.3 schema upgrade.
mig::MigrationRegistry registry;
registry.registerMigration(
    mig::SchemaVersion{1, 2, 0},
    mig::SchemaVersion{1, 3, 0},
    "Health",
    [](mig::Record& r) {
        // v1.3 split Health.current into current/regen
        r.set("regen", 0.0f);
    });

// At load time: drive the pipeline.
mig::SaveFile save = mig::readSaveFile("/tmp/quicksave.bin");
mig::Pipeline pipeline{registry};
mig::Report report = pipeline.migrate(save.records,
                                      save.metadata.schemaVersion,
                                      mig::SchemaVersion{1, 3, 0});
if (!report.warnings.empty()) {
    // ... handle missing migrations / orphaned aliases / etc.
}
// `save.records` is now in the v1.3 schema; hand to engine via
// cb.spawn(...) the usual way.
```

## Public surface

| Header | Purpose |
|---|---|
| `version.hpp` | `SchemaVersion`, `FormatVersion`, `VersionRange`, library version stamp. |
| `records.hpp` | `Record`, `RecordSet`, `FieldValue` — the serialized data model. |
| `registry.hpp` | `MigrationRegistry` — register schema upgrades + type aliases. |
| `pipeline.hpp` | `Pipeline` — execute registered upgrades end-to-end. |
| `savefile.hpp` | `SaveFile`, `SaveMetadata`, `readSaveFile`/`writeSaveFile`. |
| `world.hpp` | `WorldSnapshotMigrator` — migrate `threadmaxx::WorldSnapshot` blobs. |
| `component.hpp` | `ComponentMigrationBridge` — wire migrations to the engine's per-component serialize traits. |
| `rename.hpp` | Field / type rename helpers (split, merge, widen). |
| `transform.hpp` | Generic record-transform combinators. |
| `validation.hpp` | Pre-flight schema + orphan / alias validation. |
| `report.hpp` | `Report` — per-record warnings + counters from a pipeline run. |
| `io.hpp` | Binary reader / writer + integrity hash helpers. |
| `threadmaxx_migration.hpp` | Umbrella header. |

## Documentation

| Document | Audience | Purpose |
|---|---|---|
| [`README.md`](README.md) | Everyone | Top-level overview (this file) |
| [`DESIGN_NOTES.md`](DESIGN_NOTES.md) | Library devs | Authoritative spec — design principles, package layout, data model |
| [`FUTURE_WORK.md`](FUTURE_WORK.md) | Library devs | Original batch plan (historic; the library has since shipped) |
| [`CHANGELOG.md`](CHANGELOG.md) | Everyone | Per-release notes |

The companion `threadmaxx_studio` library renders four migration
panels — `SaveInspectorPanel`, `MigrationStepPanel`,
`SchemaGraphPanel`, `MigrationValidatorPanel` — for visualising a
schema graph, walking a pipeline step-by-step, and aggregating a
validation report across a save corpus. See
[`threadmaxx_studio/README.md`](../threadmaxx_studio/README.md).

## Design principles

1. **Versioned from day one.** Every saved record carries schema and
   migration metadata.
2. **Data-first, engine-agnostic.** Operates on serialized data,
   never on a live `World`.
3. **Component-aware.** Migration steps key on component name +
   schema version pair.
4. **Explicit upgrade steps.** No hidden magic; every transform is
   registered and individually testable.
5. **Round-trip friendly.** New builds read old saves and, where
   possible, write forward-compatible records.
6. **Small public surface.** Twelve headers, one umbrella include.
7. **Toolable.** The same logic runs in the game and in the offline
   converter — no runtime-only fast paths.
8. **Safe failure modes.** Invalid / unsupported saves fail cleanly
   with diagnostics; the engine never sees half-migrated state.

## Scope

- ✓ Schema upgrade for serialized records (per-component fields and
  whole `WorldSnapshot` blobs).
- ✓ Field rename / split / merge / widen.
- ✓ Type aliasing across versions.
- ✓ Pre-flight validation (`orphaned_aliases`, `unknown_type`,
  `no_migration_path`).
- ✓ Offline batch conversion via `threadmaxx_migration_convert`.
- ✗ Whole-engine serialization (use the engine's `WorldSnapshot`).
- ✗ Live-`World` mutation (game-side `cb.spawn` after migration).
- ✗ Asset migration (mesh / texture format upgrades belong with the
  renderer's asset loader).
- ✗ Networking, rendering, audio, physics — sibling-library
  concerns each.

## Building

Built by the top-level `CMakeLists.txt` unconditionally. Disable with
`-DTHREADMAXX_BUILD_MIGRATION=OFF`. Produces:

- Static library `threadmaxx::migration`.
- Offline tool `threadmaxx_migration_convert` (under
  `tools/migration_convert/`).
- 30 ctest entries under `tests/migration/` covering codec
  round-trips, registry / pipeline behaviour, rename helpers, and
  validation.

Bench: `bench/migration_bench` (opt-in via
`-DTHREADMAXX_BUILD_BENCHMARKS=ON`).
