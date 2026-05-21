# `threadmaxx_migration` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **not started**. All batches are 📋 planned. Sequencing
follows the §8 "implementation order" of the design notes,
regrouped into shippable units that each carry their own tests.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

The library produces a static library `threadmaxx::migration` plus
public headers. It also produces a separate executable target
`threadmaxx_migration_convert` for offline conversion runs.

The library is **data-first**: it transforms serialized records,
not live engine state. The runtime consumes the engine's existing
`WorldSnapshot` + component serialization hooks; this library
provides versioning and field-level fixups on top.

## Library structure (target end-state)

```
include/threadmaxx_migration/
  threadmaxx_migration.hpp # umbrella
  version.hpp              # SchemaVersion / FormatVersion
  registry.hpp             # MigrationRegistry
  records.hpp              # Record / RecordSet / FieldValue
  savefile.hpp             # SaveMetadata + container
  world.hpp                # WorldSnapshotMigrator
  component.hpp            # ComponentMigrationBridge
  rename.hpp               # rename / alias helpers
  transform.hpp            # FieldRemap helpers
  validation.hpp           # schema validation
  pipeline.hpp             # MigrationPipeline
  report.hpp               # MigrationResult / warnings
  detail/
    binary_reader.hpp
    binary_writer.hpp
    hash.hpp
    tombstone.hpp
    compat.hpp
src/threadmaxx_migration/
  MigrationRegistry.cpp
  MigrationPipeline.cpp
  WorldSnapshotMigrator.cpp
  ComponentMigrationBridge.cpp
  ConvertTool.cpp          # links into the offline exe
tools/migration_convert/
  main.cpp                 # standalone executable
tests/migration/
  test_migration_*.cpp
```

## Batch M1 — Foundations (versioning + records)

**Goal**: `SchemaVersion`, `FormatVersion`, `Record`, `RecordField`,
`FieldValue`, `RecordSet`, `SaveMetadata` PODs. Plus the binary
reader / writer for the record container format. No migration
logic yet — just the data model.

**Test gate**:

- `test_migration_version_compare` — SchemaVersion ordering;
  1.0.0 < 1.1.0 < 2.0.0 follows semver.
- `test_migration_record_roundtrip` — construct a Record with 3
  fields; serialize to bytes via writer, read back via reader;
  field names + values match byte-for-byte.
- `test_migration_recordset_metadata` — RecordSet's metadata is
  preserved across the roundtrip.
- `test_migration_corrupted_blob` — corrupted magic / truncated
  payload is rejected with a clear error, no crash.

**Files**: `version.hpp`, `records.hpp`, `savefile.hpp`,
`detail/binary_reader.hpp`, `detail/binary_writer.hpp`, umbrella
header, four tests.

**Risks**: lock in the record-container binary format here. Use
`magic + version + record count + per-record { typeName, stableId,
sourceVersion, field count, field bytes }` — same shape as the
engine's WorldSnapshot, with explicit version rejection on
mismatch.

**Out of scope**: registry / migrations (M3).

## Batch M2 — Save metadata + commitHash anchoring

**Goal**: `SaveMetadata` includes the engine's `commitHash` at
save time. Loading rejects saves whose commitHash conflicts with
declared compatibility rules (per the design notes §4.2: the
commitHash is "a deterministic runtime guard, so it is a natural
anchor for save validation").

**Test gate**:

- `test_migration_metadata_commit_hash` — save metadata includes
  the engine's commitHash; reload reads it back.
- `test_migration_metadata_build_id` — build IDs are
  case-sensitive strings; preserved verbatim.
- `test_migration_metadata_reject_unknown_format` — file with
  unknown FormatVersion fails loading; error includes the
  observed version.

**Files**: extension to `savefile.hpp`, hooks in
`detail/hash.hpp`.

**Out of scope**: actual migrations applied to mismatched data
(M5 — pipeline handles version traversal).

## Batch M3 — MigrationRegistry + type aliases

**Goal**: register types + their introduction version + their
aliases. Foundation that every later batch builds on.

**Test gate**:

- `test_migration_registry_register` — `registerType("Health",
  1.0.0)`; `knowsType("Health")` returns true.
- `test_migration_registry_alias` — `aliasType("HP", "Health")`;
  `knowsType("HP")` returns true; lookups via alias resolve to
  the canonical name.
- `test_migration_registry_hasPath` — register migrations from
  1.0.0 → 1.1.0 and 1.1.0 → 2.0.0; `hasPath("Health", 1.0.0,
  2.0.0)` returns true; missing intermediate path returns false.

**Files**: `registry.hpp`, `rename.hpp`,
`src/MigrationRegistry.cpp`.

**Risks**: cycle detection — `aliasType("A", "B")` and
`aliasType("B", "A")` is a configuration error, not a runtime
behavior. Reject at registration time.

**Out of scope**: applying migrations (M4 / M5).

## Batch M4 — Field rename / remap helpers

**Goal**: the boring-but-essential operations. `FieldRename`
operates on a Record (replace field name); `FieldRemap` runs a
user-supplied `transform(FieldValue&)` on a named field. Common
recipes (rename, type-widen, default-on-missing) ship as helpers.

**Test gate**:

- `test_migration_rename` — Record with field "current" → after
  applying `FieldRename{from="current", to="hp"}`, the field is
  named "hp" with the same bytes.
- `test_migration_remap_widen` — u16 field widened to u32 with a
  pre-built helper; resulting bytes parse as u32.
- `test_migration_remap_default_on_missing` — record without a
  field "layerMask" gets a default of 0xFFFFFFFF after a
  default-on-missing helper.
- `test_migration_remap_split` — split "Transform" field into
  "position" and "rotation" sub-records via a custom transform.

**Files**: `rename.hpp`, `transform.hpp` (helpers).

**Out of scope**: chained / pipelined application (M5).

## Batch M5 — MigrationPipeline

**Goal**: given a RecordSet at version A, walk the registered
migration steps until reaching the target version (or fail with a
clear diagnostic). Multi-step chains supported.

**Test gate**:

- `test_migration_pipeline_one_step` — register a 1.0.0 → 1.1.0
  migration; pipeline.migrate(input at 1.0.0) → output at 1.1.0.
- `test_migration_pipeline_multi_step` — register chains 1.0.0 →
  1.1.0 → 1.2.0 → 2.0.0; migrateToLatest from 1.0.0 input →
  output at 2.0.0; intermediate transforms all applied in order.
- `test_migration_pipeline_no_path` — request migration to a
  version with no chain; result.ok=false, error mentions the gap.
- `test_migration_pipeline_unknown_type` — input record with an
  unknown type;
  with `failOnUnknownType=true`, ok=false;
  with `failOnUnknownType=false`, kept as opaque tombstone.
- `test_migration_pipeline_warnings` — lossy migration with
  `allowLossy=true` records a warning, but ok=true.
- `test_migration_pipeline_stable_ids` — entity stable IDs
  preserved across the pipeline; verified by hash.

**Files**: `pipeline.hpp`, `report.hpp`,
`src/MigrationPipeline.cpp`, `detail/tombstone.hpp`.

**Out of scope**: world-snapshot adapter (M6).

## Batch M6 — WorldSnapshot adapter

**Goal**: convert between the engine's `WorldSnapshot` POD and the
generic `RecordSet` used by the pipeline. Both directions.

**Test gate**:

- `test_migration_snapshot_export` — engine with 100 entities →
  exportSnapshot → RecordSet with 100 records (one per entity).
- `test_migration_snapshot_import_identity` — exportSnapshot →
  importSnapshot of an unmigrated RecordSet produces an identical
  WorldSnapshot byte-for-byte.
- `test_migration_snapshot_migrated_roundtrip` — old-version
  snapshot → import via the pipeline → equivalent new-version
  WorldSnapshot loads cleanly via `World::restoreFromSnapshot()`
  (or the equivalent engine API).
- `test_migration_snapshot_stable_id_preservation` — entity stable
  IDs from the source survive the pipeline + the import.

**Files**: `world.hpp`, `src/WorldSnapshotMigrator.cpp`.

**Risks**: the engine's WorldSnapshot evolves (Batch 5 widened
ComponentSet from 32 to 64 bits, for instance — see the engine's
`kWorldSnapshotVersion = 2` history). The adapter must track the
engine's current snapshot format; document the dependency.

**Out of scope**: per-component codec bridge (M7).

## Batch M7 — Component codec bridge

**Goal**: a `ComponentCodec` registry that lets game code plug per
type-name encoders + decoders. Used both for serializing new types
not known to the engine, and for upgrading old representations.

**Test gate**:

- `test_migration_codec_register` — register a codec for a custom
  game-side `Faction` type; the bridge invokes it for matching
  records.
- `test_migration_codec_versioned` — register codecs for two
  versions of the same type-name; the bridge picks the codec
  whose SchemaVersion matches the record's sourceVersion.
- `test_migration_codec_missing` — record with a type that has no
  registered codec → bridge returns false; pipeline reports a
  warning under `keepUnknownFields=true`, an error otherwise.

**Files**: `component.hpp`, `src/ComponentMigrationBridge.cpp`.

**Out of scope**: codec auto-generation from reflection (v1.x).

## Batch M8 — Validation + reports + offline tool

**Goal**: validation diagnostics ("this RecordSet has 3 records
referencing unregistered type X") + a `MigrationReport` summary
type, plus the offline `threadmaxx_migration_convert` exe that
ingests a save file at version A and writes at version B.

**Test gate**:

- `test_migration_validation_unknown_types` — validate(RecordSet)
  reports the 3 unknown types as warnings.
- `test_migration_validation_orphaned_aliases` — alias chain that
  ends in an unregistered type fires a validation warning.
- `test_migration_report_summary` — successful migration's report
  includes record count, warning list, applied-step list.
- `test_migration_convert_tool` (smoke) — invoke the convert exe
  on a small fixture save; output file loads cleanly.

**Files**: `validation.hpp`, `report.hpp`, `src/ConvertTool.cpp`,
`tools/migration_convert/main.cpp`.

**Out of scope**: streaming conversion (large save files) — v1.x.

## v1.0 close-out criteria

- ✓ Every batch M1–M8 landed and tested.
- ✓ End-to-end: a fixture save from a v1 schema migrates through
  two intermediate versions to current schema and loads into a
  fresh engine with all entity stable IDs preserved.
- ✓ Offline conversion tool works on a sample save corpus
  (lives under `tests/migration/fixtures/`).
- ✓ Docs: README, USER_GUIDE, MAINTAINER_GUIDE, plus a
  `MIGRATION_AUTHORING_GUIDE.md` walking game authors through the
  common recipes (rename, widen, split, default-on-missing).
- ✓ ctest 100% on `build/` and `build-werror/`.
- ✓ Version stamped at 1.0.0 in
  `include/threadmaxx_migration/version.hpp`.

## v1.x candidate batches (not blocking v1.0)

### v1.x — Text / JSON record format

v1.0 ships binary only. Some games want human-readable saves for
debugging or version control. Optional JSON variant that round-
trips through the same `RecordSet`.

### v1.x — Streaming conversion

Large save files (>100 MiB) currently load into memory whole. A
streaming pipeline that processes records one at a time would let
games support arbitrarily-large worlds.

### v1.x — Reflection-driven codec generation

Manual `ComponentCodec` registration is fine for ~20 component
types, painful at 200+. Once C++26 reflection ships, the bridge
can auto-generate codecs from struct definitions.

### v1.x — Migration validation in CI

A test pattern (not a library feature) that loads every checked-in
save fixture against the current codebase and fails CI if the
pipeline can't migrate them. Worth shipping as a `tests/` example
once a game accumulates real save fixtures.

### v1.x — Multi-source diff merge

When two save streams diverged (e.g., player saved on two clients)
and a game wants to merge them. Niche but useful for some genres.

### v1.x — Schema graph visualization

Generate a Graphviz / Mermaid diagram of the migration registry's
type+version graph. Useful documentation artifact for any project
with non-trivial migration depth.

### v1.x — Compatibility tags

`SaveMetadata` could carry compatibility flags ("this save uses
feature X; if X is unsupported, refuse to load"). Game-policy
question more than a library one; ship the hook in v1.0
metadata and let game authors define their tag values.

## Out of scope for the whole library

Per DESIGN_NOTES §7 — none of this lands at any batch:

- Direct engine mutation
- File system ownership (game owns paths)
- Asset import pipeline ownership
- Physics solver logic
- Renderer logic
- Networking protocol
- Editor UI
- Hidden compatibility rules outside the registry
- Assumption that every save must be binary
- Requirement that old data is always losslessly migratable
