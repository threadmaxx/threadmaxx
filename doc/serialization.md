# Serialization

@page serialization Serialization

`<threadmaxx/Serialization.hpp>` is header-only sugar for capturing the
world's dense arrays into a flat blob and round-tripping that blob
through a stream. It is intended for save/load, snapshot/replay, and
diff-based net replication — anything that needs to freeze the
engine's authoritative state into bytes.

## What's in the header

- A `WorldSnapshot` POD that holds copies of every dense array
  (`entities`, `transforms`, `velocities`, `renderTags`, `userData`,
  `accelerations`, `parents`, `healths`, `factions`,
  `animationStates`, `physicsBodies`, `navAgents`, `boundingVolumes`,
  `masks`).
- `World::snapshot()` — captures one. Safe to keep across ticks; safe
  to serialize whenever.
- A trait pair `serialize(std::ostream&, const T&)` /
  `deserialize(std::istream&, T&)` for every built-in component (and
  for `Vec3`, `Quat`, `ComponentSet`, `WorldSnapshot` itself). The
  ComponentSet writer emits a 64-bit field (v2 format; the v1
  32-bit form is rejected by the deserializer).
- A tiny binary format with magic header `'SXMT'` and a version field;
  the deserializer rejects mismatched magic or version.

## Capturing a snapshot

```cpp
// From IGame::postStep, after engine.step() returns, or anywhere
// outside a wave system's update().
threadmaxx::WorldSnapshot snap = engine.world().snapshot();
```

`snapshot()` is a copy — index `i` of every array refers to the same
entity, and the copy is stable regardless of later spawns/destroys.

## Serializing

```cpp
std::ofstream out("save.bin", std::ios::binary);
threadmaxx::serialize(out, snap);
```

The output is a self-describing binary blob:
`[magic u32][version u32][entityCount u64][14 dense arrays each
prefixed by a u64 length]`. All values are host-endian; the format is
not portable across architectures with different endianness (a
cross-platform format isn't on any current §3 batch — file a PR if
you want one).

Per-component serializers are also available if you want to slice the
state differently:

```cpp
threadmaxx::Transform t = ...;
threadmaxx::serialize(out, t);
// matching deserialize(in, t) reads it back.
```

## Deserializing

```cpp
std::ifstream in("save.bin", std::ios::binary);
threadmaxx::WorldSnapshot snap;
if (!threadmaxx::deserialize(in, snap)) {
    // magic / version / stream EOF mismatch — discard `snap`.
    return false;
}
```

## Restoring into a fresh engine

The engine intentionally does NOT provide a "load and bypass the
command buffer" API — restoration must flow through the same commit
phase live mutations use. The pattern:

```cpp
class SaveLoader : public threadmaxx::IGame {
    threadmaxx::WorldSnapshot snap_;
public:
    explicit SaveLoader(threadmaxx::WorldSnapshot s) : snap_(std::move(s)) {}

    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& cb) override {
        for (std::size_t i = 0; i < snap_.size(); ++i) {
            // Spawn the always-present slots through the explicit-mask
            // overload, then attach the §3.1 batch-5 slots
            // individually for each entity whose mask carries them.
            cb.spawn(snap_.transforms[i],
                     snap_.velocities[i],
                     snap_.renderTags[i],
                     snap_.userData[i],
                     snap_.accelerations[i],
                     snap_.parents[i],
                     snap_.masks[i]);
            // The dense rows are still indexed by `i`; once the entity
            // exists, fire per-component setters for the batch-5 slots
            // present in this entity's mask. (Reserve a handle and
            // attach in the same recording if you need atomicity.)
            // Example:
            //   if (snap_.masks[i].has(Component::Health))
            //       cb.setHealth(handle, snap_.healths[i]);
            // — the exact dispatch shape depends on your game's
            // handle-tracking; see the test
            // `tests/new_components_test.cpp` for the round-trip
            // pattern.
        }
    }
};
```

The seeded entities get fresh handles (the source snapshot's handles
may no longer be valid). If your save needs handle-stable references
(e.g. one entity's `Parent` points at another by handle), translate
old handles to new ones during restore — typically by tracking a
`std::vector<EntityHandle>` you assemble as you spawn and rewriting
the `Parent` field before each spawn call.

## Versioning & migration

The header ships `kWorldSnapshotVersion` (currently `2`). Bump it when
the on-disk format changes; the deserializer rejects mismatches, so a
v3 binary cannot be silently loaded by a v2 build.

History:

- **v1** — pre-batch-5: 8 dense arrays, 32-bit `ComponentSet`.
- **v2** — batch 5 (2026-05-14): 14 dense arrays (adds `healths`,
  `factions`, `animationStates`, `physicsBodies`, `navAgents`,
  `boundingVolumes`), 64-bit `ComponentSet`. Tag-only categories
  (`StaticTag`, `DisabledTag`, `DestroyedTag`) ride in the mask bits
  and need no dense storage.

The engine intentionally does NOT provide a versioned migration
framework. Games that need migration write their own per-version
upgrade pass between `deserialize` and the restore game's `onSetup`.

## Per-component overloads (custom format)

The trait pair is just two free functions per type. To swap in a
different format (e.g. text, protobuf), provide your own functions —
ADL will pick them over the engine's defaults when called from your
namespace. The `WorldSnapshot` serializer is built on the per-component
functions, so a custom override propagates automatically.
