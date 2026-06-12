# Changelog

## v1.0.0 — 2026-06-12

Initial release. Hand-written spec, batched as R1 → R8 then close-out.

### Pillars

- **R1 — Foundations + aggregate reflection.** `field_count<T>()`,
  `get<I>(t)`, `for_each_field(t, fn)` for any aggregate (up to 16
  fields). `UniversalInit` SFINAE probe + structured-bindings dispatch.
- **R2 — Macro registration.** `THREADMAXX_REFLECT(T, f1, …)` emits
  an ADL hook returning `FieldList<T, FieldDescriptor…>` with names,
  offsets, types. C++20 class-type NTTP via `FixedString<N>`.
- **R3 — Runtime TypeInfo + TypeRegistry.** `shared_mutex`-locked,
  deque-backed `TypeInfo` storage so handed-out pointers stay valid.
  Lookup by name and `type_index`; idempotent `registerType<T>`.
- **R4 — Enum reflection.** `enum_name`, `enum_values`, `enum_cast`,
  `enum_count` via `__PRETTY_FUNCTION__` / `__FUNCSIG__` scraping.
  `EnumRange<E>` customization, flag-enum `|`-parsing opt-in.
- **R5 — Attribute system.** `Range`, `Tooltip`, `Hidden`, `Units`,
  `ReadOnly`, `Step` PODs; runtime `addFieldAttribute<Attr>` against
  the registry. Re-points field span on each push under the writer lock.
- **R6 — Visitor + JSON binder.** `visit_fields(obj, visitor)` alias;
  ships `PrintVisitor` / `HashVisitor` (FNV-1a-64) / `EqualsVisitor` /
  `fields_equal`. Runtime `to_json(typeInfo, obj)` + `from_json` driven
  by `FieldInfo::typeIndex` switch tables. Primitive-only in v1.0.
- **R7 — Engine bridge.** `engine_bridge::registerBuiltins(reg)`
  pulls in 14 aggregate components from `threadmaxx/Components.hpp`
  (Vec3, Quat, Transform, Velocity, Acceleration, RenderTag, UserData,
  Parent, Health, Faction, AnimationStateRef, PhysicsBodyRef,
  NavAgentRef, BoundingVolume). Gated by
  `THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1`.
- **R8 — Patch / readField.** Type-erased `Value` (40-byte SBO,
  arithmetic primitives + bool + opaque pointer + `type_index`).
  `Patch` / `PatchEntry` carries `(fieldName, Value)` pairs;
  `applyPatch(typeInfo, obj, patch)` writes by name with type-match
  guards. `readField(typeInfo, obj, name)` extracts a `Value`.

### Gates

- **Test count:** 32 reflect tests + 3 engine-bridge tests (gated).
- **Crowd no-alloc gate:** 1000 calls per frame × 100 frames after
  5-frame warmup — 0 allocations / 0 frees.
- **Headless demo:** `examples/reflect_demo` registers a struct, dumps
  JSON, applies a Patch, exits 0.
- **Warnings:** `-Wsign-conversion -Wconversion -Wold-style-cast -Werror`
  clean across the whole library.
- **Full repo:** 403/403 tests green.

### Deferred to v1.x

- Container reflection (`std::map`, `std::unordered_set`, intrusive).
- Nested aggregate JSON binding (Vec3-in-Transform renders as `null`).
- `THREADMAXX_REFLECT_WITH_ATTRS` compile-time attribute macro.
- Cross-process stable type IDs.
- Path-style patches (`Transform.position.x`).
- Hot-reload of reflection metadata.
- Free / member function reflection.
