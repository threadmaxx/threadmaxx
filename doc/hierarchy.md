# Hierarchy

@page hierarchy Hierarchy

threadmaxx supports parent-child transform attachment via the built-in
`Parent` component and the `HierarchySystem` factory.

## The `Parent` component

```cpp
struct Parent {
    EntityHandle parent      = kInvalidEntity;
    Transform    localOffset = {};
};
```

`parent` is the handle of the parent entity. `localOffset` is the child's
pose in the parent's local space. `parent == kInvalidEntity` makes the
hierarchy system ignore this entity even if the `Parent` presence bit is
set.

The presence bit is managed automatically by `setParent`:

- `cb.setParent(e, p)` where `p.parent.valid()` → bit set.
- `cb.setParent(e, Parent{})` → bit cleared, entity becomes a root.

## The hierarchy system

```cpp
engine.registerSystem(threadmaxx::makeHierarchySystem());
```

Behavior:

- Reads `Transform` and `Parent`; writes `Transform`.
- Runs single-threaded inside `ctx.single()` so multi-level chains
  resolve in one pass via DFS-with-memoization.
- Computes each parented entity's world `Transform` as
  `parent.world ∘ localOffset`:
  - **Position**: `parent.position + rotate(parent.orientation, local.position)`
  - **Orientation**: `parent.orientation * local.orientation`
    (Hamilton product of quaternions)
  - **Scale**: `local.scale` — scale is **not** chained from the parent.

### Why scale doesn't chain

A scaled parent applied component-wise to a child position is well
behaved, but applying it to the child's scale produces non-uniform
shears in any chain that mixes rotation with non-uniform scale. To keep
the engine's transform composition rotation-friendly and renderer-
agnostic, the hierarchy system treats child scale as already in world
space. If you want a child to inherit the parent's uniform scale, copy
it explicitly in user code.

This matches what a lot of real engines do for animation rigs (Unreal's
"World Space Transform" mode) and avoids surprising behavior for the
common case of a rotating mount with a child that should not stretch.

## Registration order

`HierarchySystem` must run **after** any user system that writes
`Transform`. Otherwise it will propagate the previous tick's world pose,
and the renderer will see a one-tick-stale child position.

```cpp
engine.registerSystem(std::make_unique<MovementSystem>()); // writes Transform
engine.registerSystem(std::make_unique<PhysicsSystem>());  // writes Transform
engine.registerSystem(threadmaxx::makeHierarchySystem());  // last writer of Transform
```

Because both `MovementSystem` and `HierarchySystem` declare `writes =
{Transform}`, they conflict and land in distinct waves; the hierarchy
system runs in a wave strictly after the writer's commits are visible.

If you change a parent's `localOffset` *and* its parent's pose in the
same tick, you'll need an extra step (call `engine.step()` once more) for
the change to propagate through a multi-step chain — the first step
commits the parent's new pose, the second step's hierarchy run picks it
up. `tests/hierarchy_test.cpp` documents this.

## Attaching an entity

At spawn time, with the explicit-mask overload:

```cpp
threadmaxx::Parent p{};
p.parent = parentHandle;
p.localOffset.position = {0.0f, 1.5f, 0.0f};

threadmaxx::ComponentSet mask;
mask |= threadmaxx::Component::Transform;
mask |= threadmaxx::Component::Parent;

cb.spawn(threadmaxx::Transform{}, threadmaxx::Velocity{},
         threadmaxx::RenderTag{}, threadmaxx::UserData{},
         threadmaxx::Acceleration{}, p, mask);
```

After spawn:

```cpp
cb.setParent(child, threadmaxx::Parent{ parentHandle, localOffset });
```

Detaching (entity becomes a root):

```cpp
cb.setParent(child, threadmaxx::Parent{});
```

## Reading parents

```cpp
if (const auto* p = ctx.world().tryGetParent(child); p && p->parent.valid()) {
    // child is attached to p->parent with p->localOffset
}
```

Dense iteration:

```cpp
const auto parents = ctx.world().parents();
const auto masks   = ctx.world().componentMasks();
for (std::uint32_t i = 0; i < parents.size(); ++i) {
    if (masks[i].has(threadmaxx::Component::Parent)) {
        // parents[i] is attached
    }
}
```

## Limits and what's not handled

- The hierarchy system iterates all live entities every tick (filtered by
  presence bit) and walks each parented entity's chain via DFS-with-
  memoization. This is O(N + total chain length). For typical 3D rigs
  this is fine; if you have very long chains and tight perf budgets,
  consider sorting entities so a parent always precedes its children and
  dropping the memoization.
- There's no cycle detection. Two entities that point to each other as
  parents will infinite-loop. Don't do that.
- A child whose parent has been destroyed: the destroy command bumps the
  parent's generation; the hierarchy system sees the stale handle and
  treats the child as a root for this tick. The child's `Parent` bit
  stays set until something calls `setParent(e, Parent{})`. If you want
  automatic detach-on-parent-destroy, write a small system that walks
  `parents()` and emits `setParent(e, Parent{})` for stale handles.
