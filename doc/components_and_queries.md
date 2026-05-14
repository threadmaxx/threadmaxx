# Components & Queries

@page components_and_queries Components & Queries

## Built-in components

Every live entity has the following component slots stored in parallel
dense arrays. A presence bit in the per-entity `ComponentSet` tells you
which ones are *logically* attached.

| Component | Members | Purpose |
| --- | --- | --- |
| `Transform` | `Vec3 position`, `Quat orientation`, `Vec3 scale{1,1,1}` | World-space pose. |
| `Velocity` | `Vec3 linear`, `Vec3 angular` | Per-tick rate; user systems integrate this. |
| `Acceleration` | `Vec3 linear`, `Vec3 angular` | Per-tick rate-of-change of velocity. The engine does **not** integrate it — physics systems read it. |
| `RenderTag` | `int32_t meshId`, `int32_t materialId`, `uint32_t flags` | Renderer-side hand-off. Presence bit is set iff `meshId >= 0`. |
| `UserData` | `uint64_t value` | 64 bits the engine never reads. Use freely. |
| `Parent` | `EntityHandle parent`, `Transform localOffset` | Hierarchical attachment. See [Hierarchy](hierarchy.md). |
| `Health` | `float current`, `float max` | Per-entity hit-point pool. Engine doesn't integrate it. |
| `Faction` | `uint32_t id` | Team/AI affiliation. Engine never interprets the value. |
| `AnimationStateRef` | `ResourceId<AnimationGraph> graph`, `uint32_t state`, `float t` | Pointer at an animation graph resource plus the entity's elapsed state. User-side animation systems consume it. |
| `PhysicsBodyRef` | `uint64_t handle` | Opaque handle into a sibling physics library (Jolt/Bullet/PhysX). |
| `NavAgentRef` | `uint64_t handle` | Opaque handle into a sibling navmesh library. |
| `BoundingVolume` | `Vec3 min`, `Vec3 max` | AABB for visibility-culling / broad-phase. Engine never populates it. |

Reading components in a system:

```cpp
const auto transforms = ctx.world().transforms();   // std::span<const Transform>
const auto velocities = ctx.world().velocities();   // std::span<const Velocity>
const auto healths    = ctx.world().healths();      // std::span<const Health>
```

These spans are valid for the duration of the system's `update()` —
specifically, until the engine commits the command buffers from this
system. They are dense and contiguous: index `i` of every span refers to
the same entity, which is also `world().entities()[i]`.

## Tag-only components

Three categories live in the mask but have **no dense storage**:

| Tag | Bit | Meaning |
| --- | --- | --- |
| `Component::StaticTag` | 13 | Hint: entity does not move; static-batch / spatial-hash code can opt in. |
| `Component::DisabledTag` | 14 | Skip on rendering and on user systems that honor the flag. Engine itself drops `DisabledTag` entities from `RenderFrame`. |
| `Component::DestroyedTag` | 15 | Marker that gameplay code intends to destroy this entity at the next safe point. User systems own the eventual `cb.destroy(e)` — the engine never auto-destroys on this bit. |

Flip a tag on or off without a full mask read-modify-write:

```cpp
cb.addTag   (e, Component::DisabledTag);    // OR the bit in
cb.removeTag(e, Component::DisabledTag);    // clear the bit
```

Check presence:

```cpp
if (ctx.world().hasTag(e, Component::StaticTag)) { /* ... */ }
```

`addTag` / `removeTag` are safe for concurrent workers each flipping a
different bit on the same entity — unlike `setComponentMask`, which
clobbers the entire mask in submission order.

## The per-entity ComponentSet

Look up a presence bit by handle:

```cpp
if (const auto* m = ctx.world().tryGetComponentMask(e); m && m->has(Component::RenderTag)) {
    // entity e has RenderTag presence set
}
```

Iterate dense-with-mask:

```cpp
const auto masks = ctx.world().componentMasks();    // std::span<const ComponentSet>
for (std::uint32_t i = 0; i < masks.size(); ++i) {
    if (masks[i].has(Component::Parent)) { /* ... */ }
}
```

How the mask gets set:

- `CommandBuffer::spawn(...)` derives a default mask: `Transform`,
  `Velocity`, `UserData`, `Acceleration` are always present; `RenderTag`
  iff `meshId >= 0`; `Parent` iff `parent.parent.valid()`. The §3.1
  batch-5 slots (`Health`, `Faction`, `AnimationStateRef`,
  `PhysicsBodyRef`, `NavAgentRef`, `BoundingVolume`) are **not** in the
  default mask — opt in via the explicit-mask overload, a `Bundle`, or
  the per-component setters below.
- The explicit-mask overload lets you override the default at spawn time.
- `setRenderTag` updates the bit based on the new `meshId` (set iff
  `meshId >= 0`).
- `setParent` updates the bit based on whether the new `Parent::parent` is
  a valid handle.
- `setHealth` / `setFaction` / `setAnimationStateRef` /
  `setPhysicsBodyRef` / `setNavAgentRef` / `setBoundingVolume` each
  write the dense value AND set the corresponding presence bit.
  (Attaching is the only direction these setters travel; detach via
  `removeTag(e, Component::Foo)`.)
- `setComponentMask` is the escape hatch for anything the helpers don't
  cover. Don't use it to set bits the underlying value doesn't justify —
  the engine doesn't validate consistency.
- `addTag` / `removeTag` are the single-bit composable forms.
- `addComponent<T>(e, value)` / `removeComponent<T>(e)` are the generic
  templated entries (§3.1 batch-6 prep). `addComponent` always attaches
  the presence bit, regardless of value semantics — use it when you
  want a uniform interface that ignores the per-type auto-bit rules.

## Inspecting archetype distribution

`World::archetypeSignatures()` returns a `std::vector<ArchetypeSignature>`
listing every distinct per-entity `ComponentSet` value currently in the
world plus the entity count per mask:

```cpp
for (const auto& [mask, count] : ctx.world().archetypeSignatures()) {
    std::printf("archetype 0x%llx: %u entities\n",
                static_cast<unsigned long long>(mask.bits()), count);
}
```

The output is sorted by `mask.bits()` ascending for stable ordering
across runs. Today it's an O(N) scan useful for HUD/profiling ("how
many distinct archetypes does my world have?"); after §3.1 batch-6's
chunked storage lands, each row will correspond to a physical archetype
chunk group and will become O(num archetypes) instead of O(N).

## The `Component` enum

```cpp
enum class Component : std::uint64_t {
    Transform         = 1ull << 0,
    Velocity          = 1ull << 1,
    RenderTag         = 1ull << 2,
    UserData          = 1ull << 3,
    EntityStructural  = 1ull << 4,    // scheduling-only, never on a per-entity mask
    Acceleration      = 1ull << 5,
    Parent            = 1ull << 6,
    Health            = 1ull << 7,
    Faction           = 1ull << 8,
    AnimationStateRef = 1ull << 9,
    PhysicsBodyRef    = 1ull << 10,
    NavAgentRef       = 1ull << 11,
    BoundingVolume    = 1ull << 12,
    StaticTag         = 1ull << 13,
    DisabledTag       = 1ull << 14,
    DestroyedTag      = 1ull << 15,
};
```

The underlying type is `std::uint64_t` (16 bits allocated, 48 spare).
The `EntityStructural` bit is a scheduling category: it's how a system
that does `spawn` / `destroy` declares that it touches "the set of live
entities" rather than any specific component. It never appears in a
per-entity mask.

`ComponentSet::all()` returns the union of bits 0..15 — used as the
default for `ISystem::reads()` and `writes()` so that an unannotated
system conflicts with everything else and runs strictly serially.

## Queries

`Query.hpp` ships four helpers that abstract the "grab dense spans,
write `parallelFor` by hand" pattern.

### `forEach<Components...>`

Parallel over all live entities. Callable is invoked as
`fn(EntityHandle, const C0&, const C1&, ..., CommandBuffer&)` with one
command buffer per worker chunk.

```cpp
threadmaxx::forEach<Transform, Velocity>(ctx,
    [dt](EntityHandle e, const Transform& t, const Velocity& v,
         CommandBuffer& cb) {
        Transform next = t;
        next.position = t.position + v.linear * dt;
        cb.setTransform(e, next);
    });
```

### `forEachWith<Required...>`

Same shape, but only invokes the callable for entities whose mask has all
of the requested component bits set. Use this in place of sentinel
checks like `meshId < 0`.

```cpp
threadmaxx::forEachWith<Transform, RenderTag>(ctx,
    [](EntityHandle e, const Transform&, const RenderTag&, CommandBuffer&) {
        // only fires for entities that actually carry RenderTag
    });
```

The mask check is done once per entity inside each chunk; job sizing is
identical to `forEach`.

### `forEachWithCached<Required...>` + `MaskCache`

Opt-in fast path for `forEachWith`. When the same query runs many ticks
in a row over a world whose mask shape rarely changes, the per-entity
mask test inside the hot loop is wasted work. `MaskCache` lets a system
precompute the matching indices once (in `preStep` or any other serial
point) and reuse them every tick.

```cpp
class MySystem : public ISystem {
    MaskCache cache_;
public:
    void preStep(SystemContext& ctx) override {
        cache_.rebuild(ctx.world(),
            required<Transform, Velocity>());
    }
    void update(SystemContext& ctx) override {
        forEachWithCached<Transform, Velocity>(ctx, cache_,
            [](EntityHandle e, const Transform& t, const Velocity& v,
               CommandBuffer& cb) { /* ... */ });
    }
};
```

**Invariants you own.** Rebuild the cache in `preStep` if your
required set might match a new or removed entity since the last tick.
The cache's indices become stale on any structural change between
rebuilds (spawn / destroy / `addTag` / `setComponentMask` covering the
required mask). The engine bounds-checks each cached index against the
live entity count and skips out-of-range entries; it cannot detect an
index that *is* in range but no longer matches the required mask, so
the rebuild discipline is on the caller.

When the required set is dense over the world (most entities match),
the savings come from skipping the per-entity `hasAll(required)` test
inside the hot path. When the set is sparse, the savings come from
iterating only the matching subset rather than scanning every entity.

### `forEachSerial<Components...>`

Single-threaded equivalent. Useful when the per-entity work is too cheap
to parallelize, or when the body needs to observe state across entities.

```cpp
threadmaxx::forEachSerial<Transform>(ctx,
    [&](EntityHandle, const Transform& t, CommandBuffer&) {
        bbox.expand(t.position);
    });
```

## Supported component types in queries

Every built-in POD component (Transform, Velocity, RenderTag, UserData,
Acceleration, Parent, Health, Faction, AnimationStateRef,
PhysicsBodyRef, NavAgentRef, BoundingVolume) is supported as a template
argument to all four helpers. Tag-only categories (StaticTag,
DisabledTag, DestroyedTag) are not — they have no dense storage to
reference. Use `forEachWith<...>` plus an explicit `World::hasTag`
check inside the body if you need to filter on a tag.

Adding a brand-new built-in component is a multi-layer recipe; see
`CLAUDE.md` "Adding a new built-in component" for the full checklist.

## Bypassing the helpers

When the query helpers don't fit (you need both component access *and* a
custom slicing strategy, or you want to read from another entity inside
the loop), drop down to the raw API:

```cpp
const auto entities = ctx.world().entities();
const auto count = static_cast<std::uint32_t>(entities.size());
ctx.parallelFor(count, /*grain*/ 64,
    [=](Range r, CommandBuffer& cb) {
        for (auto i = r.begin; i < r.end; ++i) {
            // … direct span indexing, custom logic …
        }
    });
```

The boids example uses this pattern because its inner loop is O(N²) and
needs to read every neighbor's position.
