# `threadmaxx_migration` — save/load versioning and data migration sibling library

## 1. Purpose

`threadmaxx_migration` handles versioned persistence for projects built on `threadmaxx`.

It is for:

* save-game compatibility across builds,
* schema evolution for components,
* data upgrades after refactors,
* backward/forward-compatible loading,
* asset or world snapshot migration,
* rename / split / merge of saved fields,
* import of older records into the current schema.

It is **not** for:

* general serialization of the whole engine,
* ECS storage ownership,
* file I/O as a core engine concern,
* runtime simulation policy,
* rendering,
* physics,
* networking,
* editor UI.

That matches the roadmap boundary: the engine already has serialization hooks, but a full migration system is a sibling concern.

## 2. Design principles

1. **Versioned from day one.** Every saved record carries schema and migration metadata.
2. **Data-first, engine-agnostic.** The library transforms serialized data, not live simulation internals.
3. **Component-aware.** Migration operates on component payloads and world snapshots.
4. **Explicit upgrade steps.** No hidden magic; every transformation is registered and testable.
5. **Round-trip friendly.** New builds should read old saves and, where possible, write forward-compatible data.
6. **No core storage coupling.** It consumes the engine’s serialization hooks instead of replacing them.
7. **Deterministic when possible.** Same input save → same migrated output.
8. **Small public surface.** Keep the API focused on registration, execution, and validation.
9. **Toolable.** The same logic should work in game code and offline migration tools.
10. **Safe failure modes.** Invalid or unsupported saves should fail cleanly with diagnostics.

## 3. Package layout

```text id="u2m4dq"
include/threadmaxx_migration/
  threadmaxx_migration.hpp // umbrella include
  version.hpp              // schema versioning and compatibility tags
  registry.hpp             // migration registry and type mappings
  records.hpp              // serialized record model
  savefile.hpp             // save/load containers and metadata
  world.hpp                // world snapshot migration helpers
  component.hpp            // component field migration helpers
  rename.hpp               // field / type rename utilities
  transform.hpp            // record transforms and fixups
  validation.hpp           // schema validation and diagnostics
  pipeline.hpp             // migration pipeline execution
  report.hpp               // migration results and warnings
  detail/
    binary_reader.hpp
    binary_writer.hpp
    hash.hpp
    tombstone.hpp
    compat.hpp
```

If you want tooling split out, keep offline converters in a separate executable target that links this library.

## 4. Core data model

### 4.1 Schema versions

```cpp id="x4j9kp"
namespace threadmaxx::migration {

struct SchemaVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};
};

struct FormatVersion {
    std::uint32_t value{};
};

struct VersionRange {
    SchemaVersion min{};
    SchemaVersion max{};
};

} // namespace threadmaxx::migration
```

### 4.2 Save metadata

```cpp id="n8r6vc"
namespace threadmaxx::migration {

struct SaveMetadata {
    std::string productName;
    std::string buildId;
    SchemaVersion schemaVersion{};
    FormatVersion formatVersion{};
    std::uint64_t worldSeed{};
    std::uint64_t commitHash{};
    std::string createdUtc;
};

} // namespace threadmaxx::migration
```

The inclusion of `commitHash` is deliberate: the roadmap already treats commit hashes as a deterministic runtime guard, so it is a natural anchor for save validation and migration checks.

### 4.3 Serialized records

Use a generic record model so migration can rewrite structure without knowing the final runtime layout.

```cpp id="c9w7ms"
namespace threadmaxx::migration {

struct FieldValue {
    std::vector<std::byte> bytes;
};

struct RecordField {
    std::string name;
    FieldValue value;
};

struct Record {
    std::string typeName;
    std::uint64_t stableId{};
    SchemaVersion sourceVersion{};
    std::vector<RecordField> fields;
};

struct RecordSet {
    SaveMetadata metadata;
    std::vector<Record> records;
};

} // namespace threadmaxx::migration
```

## 5. Public API

### 5.1 Migration registry

This is the main registration point.

```cpp id="s3m1fz"
namespace threadmaxx::migration {

using MigrationFn = std::function<void(Record&)>;

class MigrationRegistry {
public:
    void registerType(std::string typeName, SchemaVersion introduced);
    void aliasType(std::string oldName, std::string newName);

    void addMigration(std::string typeName,
                      SchemaVersion from,
                      SchemaVersion to,
                      MigrationFn fn);

    bool knowsType(std::string_view typeName) const noexcept;
    bool hasPath(std::string_view typeName,
                 SchemaVersion from,
                 SchemaVersion to) const noexcept;
};

} // namespace threadmaxx::migration
```

### 5.2 Field rename and remap helpers

```cpp id="f2v8zn"
namespace threadmaxx::migration {

struct FieldRename {
    std::string typeName;
    std::string from;
    std::string to;
};

struct FieldRemap {
    std::string typeName;
    std::string field;
    std::function<void(FieldValue&)> transform;
};

} // namespace threadmaxx::migration
```

These are the boring but essential operations that save-game compatibility usually needs: rename, split, merge, default, clamp, and convert.

### 5.3 Migration pipeline

```cpp id="r6p0yh"
namespace threadmaxx::migration {

struct MigrationOptions {
    bool allowLossy = false;
    bool failOnUnknownType = true;
    bool keepUnknownFields = true;
    bool preserveStableIds = true;
};

struct MigrationResult {
    bool ok{};
    RecordSet output;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

class MigrationPipeline {
public:
    explicit MigrationPipeline(MigrationRegistry registry);

    MigrationResult migrate(const RecordSet& input,
                            const MigrationOptions& options = {});

    MigrationResult migrateToLatest(const RecordSet& input,
                                    const MigrationOptions& options = {});
};

} // namespace threadmaxx::migration
```

### 5.4 World snapshot helpers

The roadmap already has `World::snapshot()` and a `WorldSnapshot` POD in the engine. This library should be the compatibility layer around that snapshot format, not a replacement for it. 

```cpp id="k5d1tm"
namespace threadmaxx::migration {

class WorldSnapshotMigrator {
public:
    explicit WorldSnapshotMigrator(MigrationRegistry registry);

    MigrationResult migrateSnapshot(const threadmaxx::WorldSnapshot& snapshot,
                                    const MigrationOptions& options = {});
};

} // namespace threadmaxx::migration
```

### 5.5 Component serialization bridge

This library should plug into the engine’s component serialization hook.

```cpp id="p1c7xy"
namespace threadmaxx::migration {

struct ComponentCodec {
    std::string typeName;
    SchemaVersion version{};
    std::function<Record(const void* component)> encode;
    std::function<bool(const Record& record, void* componentOut)> decode;
};

class ComponentMigrationBridge {
public:
    void registerCodec(ComponentCodec codec);
    bool hasCodec(std::string_view typeName) const noexcept;
};

} // namespace threadmaxx::migration
```

The engine already owns the basic serialize/deserialize hook; this bridge layers versioning and compatibility on top.

## 6. Integration with `threadmaxx`

### 6.1 How it fits the engine

The engine should keep doing what it already does well:

* expose component serialization hooks,
* produce `WorldSnapshot`,
* preserve stable entity IDs,
* maintain deterministic commit data.

`threadmaxx_migration` sits above that and turns old saved data into data the current build can load. The roadmap explicitly frames it that way.

### 6.2 Typical load flow

```cpp id="m8q2db"
RecordSet oldSave = readSaveFile(path);

MigrationRegistry registry = buildRegistry();
MigrationPipeline pipeline(std::move(registry));

auto result = pipeline.migrateToLatest(oldSave);
if (!result.ok) {
    // report errors, reject load
}

threadmaxx::WorldSnapshot snapshot = importSnapshot(result.output);
restoreWorld(snapshot);
```

### 6.3 Typical save flow

```cpp id="z7k5ln"
threadmaxx::WorldSnapshot snapshot = engine.world().snapshot();

RecordSet current = exportSnapshot(snapshot);
current.metadata.schemaVersion = currentSchemaVersion();
current.metadata.commitHash = engine.stats().commitHash;

writeSaveFile(path, current);
```

### 6.4 Game-side usage

A game can register transforms like:

* `Health.current` renamed to `Health.hp`,
* `Faction.id` widened from `u16` to `u32`,
* `Transform` split into `position` and `rotation`,
* `AnimationState` gained a `layerMask`,
* old `NavAgentRef` records upgraded to a new handle format.

Those are exactly the kinds of changes this library should make cheap.

## 7. What the library should not do

* no direct engine mutation,
* no file system ownership,
* no asset import pipeline ownership,
* no physics solver logic,
* no renderer logic,
* no networking protocol,
* no editor UI,
* no hidden compatibility rules outside the registry,
* no assumption that every save must be binary,
* no requirement that old data can always be made lossless.

That keeps it narrow and consistent with the roadmap’s separation between engine hooks and higher-level compatibility systems. 

## 8. Implementation order

1. schema/version types,
2. record model,
3. registry and aliases,
4. single-step field rename/remap,
5. multi-step migration pipeline,
6. world snapshot migration,
7. component codec bridge,
8. validation and reports,
9. offline conversion tool,
10. compatibility test suite.

## 9. Tests to add

* migrate old schema to new schema,
* unknown-field preservation,
* type rename and alias resolution,
* lossless vs lossy migration behavior,
* snapshot import round-trip,
* stable ID preservation,
* corrupted save rejection,
* multi-step migration chain correctness,
* deterministic migration output,
* compatibility report generation.
