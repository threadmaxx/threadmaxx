# `threadmaxx_reflect` — batch sequence + deferred items

`DESIGN_NOTES.md` is the spec. This file tracks the v1.0 batch
ordering, what's intentionally deferred to a v1.x series, and the
close-out gates.

## v1.0 batch series

### R1 — Foundations

- Library skeleton: `include/threadmaxx_reflect/`, `src/threadmaxx_reflect/`,
  `tests/reflect/`, `examples/reflect_demo/`.
- Public headers: `version.hpp`, `types.hpp`, `config.hpp`,
  `threadmaxx_reflect.hpp` umbrella.
- `THREADMAXX_REFLECT_VERSION = 9000` (pre-1.0 placeholder; bumped to
  `10000` at close-out), `version_string() = "0.9.0-dev"` likewise.
- Compile-time aggregate reflection (`aggregate.hpp`,
  `detail/aggregate_impl.hpp`): `field_count<T>()`, `get<I>(t)`,
  `for_each_field(t, fn)` for aggregates up to 32 fields via
  `UniversalInit` SFINAE probe.
- Tests: `test_reflect_no_engine_link`, `test_reflect_version`,
  `test_reflect_aggregate_count`, `test_reflect_aggregate_for_each`.
- Root `CMakeLists.txt` `option(THREADMAXX_BUILD_REFLECT … ON)` and
  the gated `add_subdirectory` block.

### R2 — Macro registration + named fields

- `macro.hpp` + `detail/macro_impl.hpp`: `THREADMAXX_REFLECT(T, …)`
  emits the ADL hook
  `_threadmaxx_reflect_fields_v1(T*) -> FieldList<…>`.
- `field_info.hpp`: `FieldList<T, FieldDescriptor<T, ptr, name>…>`
  with compile-time names, offsets (from `(char*)&t.member - (char*)&t`),
  types, and a `for_each` static method.
- `for_each_field` overload selects the macro path via ADL when
  available, falls back to aggregate. Callers don't change.
- Tests: `test_reflect_macro_basic`,
  `test_reflect_macro_named_for_each`,
  `test_reflect_macro_offsets`, `test_reflect_macro_in_namespace`
  (the macro must work inside `namespace game { struct Health { … }; }`).

### R3 — Runtime TypeInfo + TypeRegistry

- `type_info.hpp`: `TypeInfo`, `FieldInfo`, `AttributeInfo` PODs.
- `registry.hpp` + `src/Registry.cpp`: `TypeRegistry` with
  `shared_mutex`, deque-backed `TypeInfo` storage, name + type_index
  indexes, lazy default singleton.
- `registerType<T>()` picks up the R2 macro output, computes per-field
  `offset`, `sizeBytes`, `alignBytes`, `typeName`, populates
  `FieldInfo`. Idempotent on `typeid(T)`.
- `name_arena.hpp`: chained-slab string storage for runtime-registered
  names (no `std::string` per `FieldInfo`).
- Auto-registration from the macro: static-init guard registers `T`
  into `TypeRegistry::defaultInstance()`. Game code can call
  `registerType<T>` against a custom registry to opt out.
- Tests: `test_reflect_registry_idempotent`,
  `test_reflect_registry_lookup_by_name`,
  `test_reflect_registry_lookup_by_type_index`,
  `test_reflect_registry_concurrent` (N threads register +
  read; data race clean under TSAN).

### R4 — Enum reflection

- `enum.hpp` + `detail/enum_impl.hpp`: `enum_name<V>()`,
  `enum_values<E>()`, `enum_cast<E>(string_view)`,
  `enum_count<E>()`.
- Scan range `[-128, 128]` default, customizable via
  `template <> struct EnumRange<MyEnum> { ... };`.
- Flag-enum opt-in: `EnumRange<E>::isFlag = true` enables OR-of-name
  rendering and bit-by-bit `enum_cast`.
- Tests: `test_reflect_enum_basic`, `test_reflect_enum_round_trip`,
  `test_reflect_enum_custom_range`, `test_reflect_enum_flags`.

### R5 — Attributes

- `attributes.hpp`: `Range`, `Tooltip`, `Hidden`, `Units`,
  `ReadOnly`, `Step`, `Choice` (enum-like dropdown for string fields).
- `THREADMAXX_REFLECT_WITH_ATTRS(T, (field, Attr...), …)` macro form.
- `FieldInfo::attributes` is populated when registration runs.
- Tests: `test_reflect_attr_range`, `test_reflect_attr_tooltip`,
  `test_reflect_attr_multiple`, `test_reflect_attr_query`.

### R6 — Visitor + JSON binder

- `visit.hpp`: `visit_fields(value, visitor)` and trait helpers.
- `binders/json.hpp` + `src/JsonBinder.cpp`: `JsonVisitor`,
  `to_json(typeInfo, obj) -> string`, `from_json(typeInfo, obj,
  string_view) -> ReflectResult<void>`.
- `PrintVisitor`, `HashVisitor` (FNV-1a-64), `EqualsVisitor` ship
  alongside.
- Tests: `test_reflect_visit_aggregate`,
  `test_reflect_visit_macro`, `test_reflect_json_round_trip`,
  `test_reflect_hash_visitor`,
  `test_reflect_equals_visitor`.

### R7 — Engine bridge

- `engine_bridge.hpp` + `src/EngineBridge.cpp` (opt-in via
  `TARGET threadmaxx::threadmaxx`).
- `registerBuiltins(reg)` registers every threadmaxx built-in
  component (`Transform`, `Velocity`, `Acceleration`, `Health`,
  `Faction`, `Parent`, `MeshRef`, `AnimationStateRef`,
  `PhysicsBodyRef`, `NavAgentRef`, `BoundingVolume` plus tag-only
  categories listed as zero-field types).
- `registerUserComponentType<T>()` integrates with
  `Engine::registerUserComponent<T>()`: when the user component's
  `T` is reflected, the reflect registry picks it up.
- `THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1` export.
- Tests (gated): `test_reflect_engine_bridge_builtins`,
  `test_reflect_engine_bridge_user_component`,
  `test_reflect_engine_bridge_idempotent`.

### R8 — Patch / apply

- `value.hpp` + `src/Value.cpp`: 40-byte SBO `Value`,
  `Value::is<T>()`, `Value::as<T>()`,
  `Value::make<T>(v)`, `Value::typeIndex()`.
- `patch.hpp` + `src/Patch.cpp`: `PatchEntry`, `Patch`,
  `applyPatch(typeInfo, obj, patch)`, `readField(typeInfo, obj,
  path) -> ReflectResult<Value>`.
- Path parser supports `.field`, `[idx]`, and nested
  `Inventory.slots[3].name`.
- Tests: `test_reflect_value_sbo`, `test_reflect_patch_basic`,
  `test_reflect_patch_nested`, `test_reflect_patch_vector_index`,
  `test_reflect_patch_unknown_field`,
  `test_reflect_read_field_round_trip`.

### v1.0 close-out

- Version bump: `THREADMAXX_REFLECT_VERSION = 10000`,
  `version_string() = "1.0.0"`.
- Docs: `README.md`, `USER_GUIDE.md`, `MAINTAINER_GUIDE.md`,
  `CHANGELOG.md`.
- Crowd no-alloc gate:
  `tests/reflect/test_reflect_crowd_no_alloc.cpp` —
  1000 `find` + `readField` + `enum_cast` calls per frame × 100
  frames after a 5-frame warmup, 0 allocs / 0 frees.
- Headless `examples/reflect_demo`:
  - register a struct via `THREADMAXX_REFLECT_WITH_ATTRS`,
  - dump it as JSON,
  - apply a Patch from a literal string,
  - read a field by path,
  - exit 0 on clean round-trip.
- `FUTURE_WORK.md` flipped to "v1.0.0 shipped" footer.
- Test sweep: `build/` + `build-werror/` all reflect tests pass,
  full repo suite green.

## Deferred (out of v1.0)

- **Container reflection beyond `std::vector`/`std::array`.** Maps,
  sets, and intrusive containers need a binder discipline that's
  bigger than v1.0 scope — they go in v1.1 with a
  `FieldBinder<C>` extension point.
- **Polymorphism / `dynamic_cast` replacement.** `OpaquePtr` carries
  `type_index`; future versions can add a virtual `TypeInfo*`
  accessor for base-typed pointers, but the engine doesn't need
  polymorphism today.
- **Cross-process stable type IDs.** Today the binding is the
  name string. A future v1.x can add an FNV-1a-64 hash of the
  name as a stable `TypeId` for replication / save games.
- **Bit-stable serialization to a versioned wire format.** That's
  `WorldSnapshot`'s job; `to_json` is for diagnostics only.
- **Hot reload of reflection metadata.** Today registration is
  startup-only. A v1.x extension can `replaceType<T>()` for plugin
  reload; the editor will need to refresh its `TypeInfo*` cache
  when this lands.
- **Reflection of free functions / member functions.** Out of scope
  — the editor wants component / resource introspection, not
  scripting. Lua / sol2-style binding sits in a future
  `threadmaxx_script` if it lands.
- **Reflection-driven UI widget mapping.** That's editor's job:
  the editor reads `FieldInfo` + `attributes` and picks the
  widget. Reflect doesn't ship widgets.
