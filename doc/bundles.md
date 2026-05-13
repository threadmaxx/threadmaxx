# Bundles

@page bundles Bundles (spawn helpers)

A `Bundle` packages a set of component values together with a
compile-time-derived component-presence mask. Feed it to
`CommandBuffer::spawnBundle` to spawn an entity in one call:

```cpp
auto enemyTemplate = bundle(
    Transform{},
    Velocity{},
    RenderTag{/*meshId*/ 7});

cb.spawnBundle(enemyTemplate);
```

The `initialMask` for the resulting entity is the union of the listed
component types. That's the meaningful difference from the
default-mask `cb.spawn(...)` overload, which always sets
Transform+Velocity+UserData+Acceleration regardless of what you passed.

## Why a separate name?

`spawnBundle` is named distinctly from `spawn` to keep braced-init
calls like `cb.spawn({})` unambiguous against the Transform-first
overload. The compile-time cost of either is identical — the bundle's
`initialMask` is constant-folded.

## Reserved handles

`spawnBundle(EntityHandle reserved, const Bundle& b)` materializes a
pre-reserved slot. Pair with `ctx.reserveHandle()` /
`engine.reserveEntityHandle()` when a job needs to spawn a parent and
its child in a single recording:

```cpp
auto parent = ctx.reserveHandle();
auto child  = ctx.reserveHandle();
cb.spawnBundle(parent, bundle(Transform{}));
cb.spawnBundle(child,
    bundle(Transform{}, Parent{parent, Transform{}}));
```

## Limitations

- A `Bundle` carries one value per built-in component slot. Passing the
  same type twice in `bundle(...)` is allowed but pointless: the last
  value wins and the mask bit is set exactly once.
- The variadic factory only accepts the built-in component types
  (`Transform`, `Velocity`, `RenderTag`, `UserData`, `Acceleration`,
  `Parent`). Anything else is a compile-time error.
