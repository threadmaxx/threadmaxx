# `threadmaxx_reflect` — maintainer guide

Conventions for shipping changes inside threadmaxx_reflect.

## Versioning

`include/threadmaxx_reflect/version.hpp` is the single source of truth:

- `THREADMAXX_REFLECT_VERSION_MAJOR/MINOR/PATCH` — integer constants.
- `THREADMAXX_REFLECT_VERSION = MAJOR*10000 + MINOR*100 + PATCH`.
- `version_string()` — human-readable, mirrors the macros.

All three move together. The pin test
`tests/reflect/test_reflect_version.cpp` enforces it.

## ABI contract

Layout of `TypeInfo`, `FieldInfo`, `AttributeInfo`, `Value` is pinned
at v1.0.0. **Appending** fields is a minor bump; reordering or
removing is a major bump.

Templated public types (`FieldDescriptor`, `FieldList`,
`EnumRange<E>`) are header-only; consumers re-instantiate against the
new layout on rebuild, so they're more forgiving — but document any
breaking change in `CHANGELOG.md`.

## Hot-path discipline

Read paths are zero-allocation after warmup. When extending:

- `TypeRegistry::find` is one `shared_lock` + one `unordered_map` find.
  Don't add per-call allocations.
- `enum_name(value)` walks a `std::array` of pairs — keep it linear,
  no hash maps or string interning at lookup time.
- The macro path's `for_each_field` is a fold expression — keep it
  branchless inside the visitor dispatch.

The crowd no-alloc gate `tests/reflect/test_reflect_crowd_no_alloc.cpp`
runs 1000 `find` + `readField` + `enum_cast` calls × 100 frames; any
allocation regresses it.

## Adding a new attribute

1. Define a POD in `attributes.hpp` with `static constexpr kName` and
   a `formatPayload(char* buf, std::size_t cap) -> std::size_t`
   method.
2. No registry changes — `addFieldAttribute<Attr>` already routes
   through the new POD via `Attr::kName`.
3. Add a test in `tests/reflect/`.

## Adding a new engine built-in

When a new aggregate component lands in `threadmaxx/Components.hpp`:

1. Add `THREADMAXX_REFLECT(NewType, f1, f2, …);` inside
   `src/threadmaxx_reflect/EngineBridge.cpp`'s `namespace threadmaxx`
   block.
2. Add the `reg.registerType<NewType>("NewType")` call in
   `registerBuiltins`.
3. Bump the expected count in
   `tests/reflect/test_reflect_engine_bridge_builtins.cpp` (currently
   14).

## Adding a new primitive to the JSON binder

`src/threadmaxx_reflect/JsonBinder.cpp` has two switch tables
(`renderField` for write, `writeField` for read). Add an
`else if (ti == typeid(NewT))` arm in both, plus a primitive helper
template specialization if the format differs.

## Adding a new binder

Drop a header under `include/threadmaxx_reflect/binders/` and an
implementation in `src/threadmaxx_reflect/`. Binders should consume
`TypeInfo` and not poke registry internals — that's the boundary that
keeps registry refactors cheap.

## Coding rules

- No exceptions on the hot path; `ReflectResult<T>` is the failure shape.
- `std::string_view` over `const std::string&` for names.
- `std::shared_mutex` reader-prefer for the registry; writer lock for
  registration only.
- Stable-pointer storage (`std::deque`) for `TypeInfo`; never
  invalidate handed-out pointers.
- Always update `THREADMAXX_REFLECT_PUBLIC_HEADERS` when adding a new
  public header to `include/threadmaxx_reflect/`.

## Testing rules

Every new public symbol gets a test in `tests/reflect/`. Tests are
single-file, link only `threadmaxx::reflect` (and `threadmaxx::threadmaxx`
when exercising the engine bridge), and follow the `Check.hpp` harness
shape.

## Releases

1. Update `version.hpp` (major/minor/patch + `version_string`).
2. Update `CHANGELOG.md` with the highlights.
3. Update `FUTURE_WORK.md` to reflect what shipped and what's deferred.
4. Refresh the test count in `README.md`'s pillar table if features
   changed substantively.
5. Tag the commit `v<MAJOR>.<MINOR>.<PATCH>`.
