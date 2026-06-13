# Changelog — `threadmaxx_migration`

All notable changes to this sibling library. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) loosely.
Older entries are immutable; new releases append.

## [1.0.0]

First production release. The M1–M8 batch sequence closed; 30
headless tests pin the contract on `build/` and `build-werror/`.

### Added (M1 — Versioning primitives)

- `SchemaVersion` POD (`major.minor.patch`, lexicographic ordering)
- `FormatVersion` POD (container-layout tag)
- `VersionRange` POD + `kCurrentFormatVersion`
- Library version stamp (`kLibraryVersion*` + `kLibraryVersionString`)

### Added (M2 — Record model)

- `Record` / `RecordSet` — typed key/value field store
- `FieldValue` variant — engine PODs (`Vec3`, `Quat`, scalars, blobs)
- Binary reader / writer (`detail/binary_reader.hpp`,
  `detail/binary_writer.hpp`) with FNV-1a integrity hash
- Codec round-trip pinning for every built-in `FieldValue` alternative

### Added (M3 — Save file container)

- `SaveFile` + `SaveMetadata` (build id, commit hash, schema version,
  format version)
- `readSaveFile` / `writeSaveFile` with magic + version header
- Rejection of unknown format versions with a typed error

### Added (M4 — Migration registry)

- `MigrationRegistry::registerMigration` — keyed on
  `(from, to, typeName)`
- `MigrationRegistry::registerAlias` — type rename across versions
- `MigrationRegistry::hasPath` — graph reachability query
- Idempotent re-registration semantics

### Added (M5 — Pipeline)

- `Pipeline::migrate(records, fromVersion, toVersion)` —
  multi-step BFS through the registered graph
- `Report` — per-record warnings, counters, durations
- Stable record-id preservation across multi-step migrations
- "No path" / "unknown type" diagnostic emission

### Added (M6 — Rename helpers)

- `rename::field` — straight rename
- `rename::splitField` — one field → many
- `rename::widenField` — narrowing-type fixup
- `rename::defaultOnMissing` — populate missing field with default

### Added (M7 — World snapshot bridge)

- `WorldSnapshotMigrator` — drive the same pipeline against a
  serialized `threadmaxx::WorldSnapshot` blob
- `ComponentMigrationBridge` — wire registered migrations into the
  engine's per-component `serialize` / `deserialize` traits
- Round-trip pinning: `snapshot → migrate → snapshot` is byte-stable
  when no upgrade steps apply

### Added (M8 — Validation + tool)

- `Validation` — pre-flight checks (`orphaned_aliases`,
  `unknown_type`, `no_migration_path`) returning a `Report`
- `threadmaxx_migration_convert` — offline converter executable
  under `tools/migration_convert/`. Drives a `Pipeline` against an
  input directory of saves, writes upgraded outputs, emits a summary
  `Report` to stdout
- 30 test executables under `tests/migration/` covering every
  registered behaviour

### Performance

- Pipeline overhead is dominated by record I/O; the in-memory
  migrate step is `O(records × upgrades)` linear, no allocations
  beyond the per-record `FieldValue` storage.
- `bench/migration_bench` (opt-in) measures end-to-end throughput
  on synthetic record sets.

### Test footprint

- 30 migration tests, all green on `build/` and `build-werror/`.
- Includes round-trip, codec, registry, pipeline, rename, snapshot,
  validation, and offline-tool entry points.
