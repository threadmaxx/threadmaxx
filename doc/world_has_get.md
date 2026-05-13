# World::has / World::get

@page world_has_get World::has<T> and World::get<T>

Header-only sugar on `World` for the common "does this entity carry T?"
and "give me a const reference to T" patterns. Both consult the per-
entity component-presence mask, so an entity that has a dense slot for
`T` but doesn't logically own it (mask bit cleared) reads as
`has<T>() == false`.

```cpp
EntityHandle e = ...;

if (world.has<RenderTag>(e)) {
    const auto& tag = world.get<RenderTag>(e);
    // ...
}
```

## What's checked

`world.has<T>(e)` returns true iff:

1. `e` is alive (`alive(e) == true`), AND
2. `e`'s component mask has the bit for `T`.

`world.get<T>(e)` returns a `const T&`. It asserts that `has<T>(e)` is
true. Use `tryGetTransform`/`tryGetRenderTag`/etc. when absence is
legal and you want a `nullptr` instead of an assert.

## Supported types

The same set the queries (`forEach<T>`, `forEachWith<T>`) cover:

- `Transform`
- `Velocity`
- `RenderTag`
- `UserData`
- `Acceleration`
- `Parent`

Asking for a non-built-in type is a compile-time error.

## Cost

Both methods are header-only and inline. They consult the component-
presence mask first (one bit test), then index the dense array; no
function call, no allocation.

## Relationship to tryGet*

The `tryGet*(EntityHandle)` accessors return `nullptr` only if the
handle is stale; they do NOT consult the mask. That's by design — the
dense storage always has a slot per live entity. Use `has<T>` /
`get<T>` when you want presence semantics; use `tryGet*` when you want
"is this entity alive at all?".
