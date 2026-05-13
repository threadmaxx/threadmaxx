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

Reading components in a system:

```cpp
const auto transforms = ctx.world().transforms();   // std::span<const Transform>
const auto velocities = ctx.world().velocities();   // std::span<const Velocity>
```

These spans are valid for the duration of the system's `update()` —
specifically, until the engine commits the command buffers from this
system. They are dense and contiguous: index `i` of every span refers to
the same entity, which is also `world().entities()[i]`.

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
  iff `meshId >= 0`; `Parent` iff `parent.parent.valid()`.
- The explicit-mask overload lets you override the default at spawn time.
- `setRenderTag` updates the bit based on the new `meshId` (set iff
  `meshId >= 0`).
- `setParent` updates the bit based on whether the new `Parent::parent` is
  a valid handle.
- `setComponentMask` is the escape hatch for anything the helpers don't
  cover. Don't use it to set bits the underlying value doesn't justify —
  the engine doesn't validate consistency.

## The `Component` enum

```cpp
enum class Component : std::uint32_t {
    Transform        = 1u << 0,
    Velocity         = 1u << 1,
    RenderTag        = 1u << 2,
    UserData         = 1u << 3,
    EntityStructural = 1u << 4,   // scheduling-only, never on a per-entity mask
    Acceleration     = 1u << 5,
    Parent           = 1u << 6,
};
```

The `EntityStructural` bit is a scheduling category: it's how a system
that does `spawn` / `destroy` declares that it touches "the set of live
entities" rather than any specific component. It never appears in a
per-entity mask.

`ComponentSet::all()` returns the union of bits 0..6 — used as the
default for `ISystem::reads()` and `writes()` so that an unannotated
system conflicts with everything else and runs strictly serially.

## Queries

`Query.hpp` ships three helpers that abstract the "grab dense spans,
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

Today the query helpers cover `Transform`, `Velocity`, `RenderTag`, and
`UserData`. Adding `Acceleration` or `Parent` is one line each in
`Query.hpp::detail::getSpan` and `componentBit` — see the
"Adding a new built-in component" recipe in `CLAUDE.md` for the full
cross-layer checklist if you're adding a brand-new component.

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
