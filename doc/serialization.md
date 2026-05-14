# Serialization

@page serialization Serialization

`<threadmaxx/Serialization.hpp>` is header-only sugar for capturing the
world's dense arrays into a flat blob and round-tripping that blob
through a stream. It is intended for save/load, snapshot/replay, and
diff-based net replication — anything that needs to freeze the
engine's authoritative state into bytes.

## What's in the header

- A `WorldSnapshot` POD that holds copies of every dense array
  (`entities`, `transforms`, `velocities`, …, `masks`).
- `World::snapshot()` — captures one. Safe to keep across ticks; safe
  to serialize whenever.
- A trait pair `serialize(std::ostream&, const T&)` /
  `deserialize(std::istream&, T&)` for every built-in component (and
  for `Vec3`, `Quat`, `ComponentSet`, `WorldSnapshot` itself).
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
`[magic u32][version u32][entityCount u64][8 dense arrays each
prefixed by a u64 length]`. All values are host-endian; the format is
not portable across architectures with different endianness (a
cross-platform format is in §3.4 batch 7).

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
            cb.spawn(snap_.transforms[i],
                     snap_.velocities[i],
                     snap_.renderTags[i],
                     snap_.userData[i],
                     snap_.accelerations[i],
                     snap_.parents[i],
                     snap_.masks[i]);
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

The header ships `kWorldSnapshotVersion` (currently `1`). Bump it when
the on-disk format changes; the deserializer rejects mismatches, so a
v2 binary cannot be silently loaded by a v1 build.

The engine intentionally does NOT provide a versioned migration
framework. Games that need migration write their own per-version
upgrade pass between `deserialize` and the restore game's `onSetup`.

## Per-component overloads (custom format)

The trait pair is just two free functions per type. To swap in a
different format (e.g. text, protobuf), provide your own functions —
ADL will pick them over the engine's defaults when called from your
namespace. The `WorldSnapshot` serializer is built on the per-component
functions, so a custom override propagates automatically.
