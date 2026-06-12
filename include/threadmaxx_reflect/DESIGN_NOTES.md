# `threadmaxx_reflect` — compile-time + runtime reflection sibling library

## 1. Purpose

`threadmaxx_reflect` is the introspection layer for projects built on
`threadmaxx`. It owns the machinery that turns POD components — engine
built-ins, `UserComponent<T>`s, game-side structs — into a uniform,
walkable description: field names, types, offsets, attributes, enum
labels, and the ability to set/get individual fields from a type-erased
visitor.

It is for:

- compile-time aggregate reflection of trivially-walkable PODs
  (`field_count<T>`, `get<I>(t)`, `for_each_field(t, fn)`) so codegen
  paths can fan out fields without any runtime cost,
- a single registration macro (`THREADMAXX_REFLECT(T, f1, f2, …)`) that
  attaches **named** field metadata to a type — no header rewrites of
  the POD itself, just one line below the struct,
- a runtime `TypeInfo` + `TypeRegistry` so editor / debug / scripting /
  serialization paths can enumerate types they didn't see at compile
  time (e.g. mod-loaded user components),
- enum reflection (`enum_name(value)`, `enum_values<E>()`) via a
  bounded `__PRETTY_FUNCTION__` scan, customizable per-enum,
- per-field attributes (`Range`, `Tooltip`, `Hidden`, `Units`,
  `ReadOnly`) for editor property panels,
- a tiny `Visitor` abstraction that walks a value's fields and a
  reference `JsonVisitor` binder that emits human-readable JSON,
- type-erased `Value` + `Patch` so an editor / network layer can express
  "set `Health.current` on entity X to 42" without knowing the C++
  type at compile time,
- an opt-in engine bridge that auto-registers the engine's built-in
  components (`Transform`, `Velocity`, `Health`, …) and any
  `UserComponent<T>` whose `T` is reflected.

It is **not** for:

- runtime code generation, scripting language interpreters, or "compile
  C++ at runtime" — out of scope,
- non-aggregate types with private state, virtual bases, multiple
  inheritance, or non-trivial copy semantics — reflect targets PODs,
- polymorphic dispatch / `dynamic_cast` replacement — runtime type
  identity uses `std::type_index`, same as the rest of the engine,
- generalized container reflection (`std::map`, `std::unordered_set`,
  custom intrusive lists) — v1.0 supports `std::vector<T>`, `std::array`,
  and arithmetic primitives; everything else is opt-in via a user-
  written `FieldBinder<T>` specialization,
- serializing into a versioned wire format — that's `WorldSnapshot`'s
  job; reflect's JSON binder is for diagnostics, not save games,
- cross-process or cross-platform stable type IDs — `type_index` is
  per-process; the binding for cooked builds is the type's registered
  **name string**, not its address.

That puts the cut where every engine I've reviewed draws it naturally:
**above the engine's POD shapes, below any UI / scripting / editor
tool that needs to walk them**. The editor consumes reflect; reflect
never sees the editor.

## 2. Design principles

1. **Compile-time first.** The aggregate path (`field_count`, `get<I>`,
   `for_each_field`) costs nothing at runtime — it's pure structured-
   bindings unpacking. Codegen / serializers / SIMD lanes prefer it
   when they know `T` statically.
2. **Header-only public API where reasonable.** Aggregate reflection,
   the macro, attributes, enum reflection, and the visitor traits are
   all header-only. Only the runtime `TypeRegistry` carries a `.cpp`
   (it owns a `shared_mutex`-guarded map + a strings arena).
3. **One registration macro, one line.** `THREADMAXX_REFLECT(Health,
   current, max, regen)` placed in the same TU as `Health`'s
   definition. The macro emits a `friend constexpr auto
   threadmaxx_reflect_fields(Health*) { … }` ADL hook plus an inline
   `TypeRegistry` self-registration object guarded by `static_init`.
4. **Macro is opt-in, not mandatory.** Types that don't register still
   work with the compile-time aggregate path (indexed access only, no
   names). Editor falls back to "field_0, field_1, …" when no macro
   was used.
5. **No exceptions, no RTTI mandate.** `type_index` is fine (it's
   already in use across the engine). No `dynamic_cast`. No
   `std::any` (slow allocation path; we ship a SBO `Value` instead).
6. **Deterministic.** Field order matches the order the macro lists
   them in; that matches the declaration order in the POD by
   convention. Two runs registering the same types produce the same
   `TypeInfo` byte-for-byte and the same `TypeId` namespace.
7. **Thread-safe registry, lock-free read path.** `TypeRegistry::get`
   uses a `shared_mutex` reader-lock; registration is rare (startup +
   plugin load) and takes the writer. Once a `TypeInfo*` is returned
   it never moves — pointers held by the editor remain valid for the
   registry's lifetime.
8. **No engine coupling by default.** The static lib
   `threadmaxx::reflect` links nothing but the STL. The engine bridge
   (R7) opts in via a `TARGET threadmaxx::threadmaxx` check at
   configure time and exports `THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1`
   so consumers can `#if`-gate.
9. **Zero-alloc steady state.** After registration is done, the hot
   path — `TypeRegistry::find(typeName)`, `FieldInfo::get(ptr)`,
   `visit_fields(v, visitor)` — is pure pointer / offset arithmetic.
   No `std::function`, no `std::any`. Visitors are templates on the
   call site.
10. **SemVer ABI contract.** Same shape as every other sibling: integer
    `THREADMAXX_REFLECT_VERSION = MAJOR*10000 + MINOR*100 + PATCH`,
    plus `version_string()`. Public layout of `TypeInfo`, `FieldInfo`,
    and `Attribute` is pinned at v1.0.0; adding fields to those is a
    minor-version bump.

## 3. Package layout

```
include/threadmaxx_reflect/
  threadmaxx_reflect.hpp     # umbrella include
  config.hpp                 # capacity caps, enum scan range
  version.hpp                # THREADMAXX_REFLECT_VERSION + version_string()
  types.hpp                  # TypeId, FieldId, ErrorCode, ReflectResult<T>
  aggregate.hpp              # field_count, get<I>, for_each_field (compile-time)
  macro.hpp                  # THREADMAXX_REFLECT(T, …) macro
  field_info.hpp             # compile-time FieldList<T>, FieldDescriptor<T,I>
  attributes.hpp             # Range, Tooltip, Hidden, Units, ReadOnly
  enum.hpp                   # enum_name, enum_values, enum_count
  type_info.hpp              # runtime TypeInfo + FieldInfo + AttributeInfo
  registry.hpp               # TypeRegistry (shared_mutex + name index)
  value.hpp                  # type-erased Value with SBO
  visit.hpp                  # visit_fields(value, visitor) + traits
  patch.hpp                  # Patch (ordered list of (field-path, Value))
  binders/
    json.hpp                 # JsonVisitor + to_json/from_json
  engine_bridge.hpp          # opt-in IGame-style auto-registrar (R7)
  detail/
    aggregate_impl.hpp       # structured-bindings unpacking up to N=32
    macro_impl.hpp           # field-list emission helper
    enum_impl.hpp            # __PRETTY_FUNCTION__ scan
    name_arena.hpp           # registry's owned string storage
```

```
src/threadmaxx_reflect/
  Registry.cpp               # TypeRegistry impl
  Value.cpp                  # Value SBO impl
  Patch.cpp                  # Patch::apply impl
  JsonBinder.cpp             # JsonVisitor heavy lifting
  EngineBridge.cpp           # opt-in: auto-register threadmaxx built-ins
```

## 4. Core data model

### 4.1 Compile-time field list

The macro emits, for `Health{ int current; int max; float regen; }`:

```cpp
constexpr auto threadmaxx_reflect_fields(Health*) {
    using namespace ::threadmaxx::reflect;
    return FieldList<Health,
        FieldDescriptor<Health, &Health::current, "current">,
        FieldDescriptor<Health, &Health::max,     "max">,
        FieldDescriptor<Health, &Health::regen,   "regen">
    >{};
}
```

ADL lookup on `threadmaxx_reflect_fields(static_cast<T*>(nullptr))`
gives every caller the field list constexpr without polluting `T`'s
namespace with helper templates.

### 4.2 Aggregate fallback

For types that did **not** use the macro, `field_count<T>` recovers an
indexed count via structured-bindings detection (a constexpr probe up
to N = 32). `for_each_field(t, fn)` then unpacks via
`auto& [f0, f1, …] = t;` and dispatches. Names are unavailable in this
path — the editor renders `field_0`, `field_1`, …. The shared
`for_each_field` overload picks the macro path when present (via
ADL) and the aggregate path otherwise — call sites don't change.

### 4.3 Runtime TypeInfo

```cpp
struct AttributeInfo {
    std::string_view name;     // e.g. "Range"
    std::string_view payload;  // e.g. "0.0,100.0"
};

struct FieldInfo {
    std::string_view  name;             // "current"
    std::string_view  typeName;         // "int"
    std::type_index   typeIndex;
    std::uint32_t     offset;           // bytes from struct base
    std::uint32_t     sizeBytes;
    std::uint32_t     alignBytes;
    std::span<const AttributeInfo> attributes;
    // typed accessors (header-only): get<T>(void*), set<T>(void*, const T&)
};

struct TypeInfo {
    std::string_view name;
    std::type_index  typeIndex;
    std::uint32_t    sizeBytes;
    std::uint32_t    alignBytes;
    std::span<const FieldInfo> fields;
};
```

Both structs are PODs the registry stores by value in an arena —
`std::span` and `std::string_view` reference arena-owned storage.

### 4.4 Value (SBO type-erased holder)

`Value` carries one of: `void` (empty), an arithmetic primitive,
`std::string`, or a pointer + `std::type_index` for "anything else"
(the editor still finds the type in the registry, just can't pretty-
print it without a binder). 32-byte SBO covers every primitive +
small strings + `Vec3`-sized values without heap traffic.

### 4.5 Patch

```cpp
struct PatchEntry {
    std::string fieldPath;   // "Health.current" or "Inventory.slots[3].id"
    Value       newValue;
};
struct Patch {
    std::vector<PatchEntry> entries;
};
```

`apply(typeInfo, void* objBase, const Patch&) -> ReflectResult<void>` walks
each entry, resolves the path through `FieldInfo`, and writes
`newValue` into the right offset. Path parsing supports `.member` and
`[index]` for `std::vector` / `std::array` fields.

## 5. Public API surface

### 5.1 Compile-time (`aggregate.hpp`, `field_info.hpp`)

```cpp
template <typename T>
constexpr std::size_t field_count();

template <std::size_t I, typename T>
constexpr decltype(auto) get(T& obj);

template <typename T, typename Fn>
constexpr void for_each_field(T& obj, Fn&& fn);
// fn is invoked as fn(name, value) when names are available,
// or fn(index, value) otherwise.
```

### 5.2 Macro (`macro.hpp`)

```cpp
THREADMAXX_REFLECT(Health, current, max, regen);

THREADMAXX_REFLECT_WITH_ATTRS(Health,
    (current, threadmaxx::reflect::Range{0, 100}),
    (max,     threadmaxx::reflect::Range{1, 999}),
    (regen,   threadmaxx::reflect::Units{"hp/s"}));
```

Both forms emit the ADL `threadmaxx_reflect_fields` hook AND a
static-init object that registers `Health` in the **default**
`TypeRegistry` (lazy-initialized singleton). Game code that wants
explicit control over registration uses `TypeRegistry::registerType<T>()`
on a registry of its own.

### 5.3 Enum (`enum.hpp`)

```cpp
template <auto V>     constexpr std::string_view enum_name();
template <typename E> constexpr std::size_t      enum_count();
template <typename E> std::span<const std::pair<E, std::string_view>>
                                                enum_values();
template <typename E> std::optional<E>          enum_cast(std::string_view);
```

Scan range defaults to `[-128, 128]`; opt in to a wider range by
specializing `EnumRange<E>::min` / `max`. Bitflag enums get an
`EnumRange<E>::isFlag = true` opt-in for `OR`-of-name rendering.

### 5.4 Runtime registry (`registry.hpp`, `type_info.hpp`)

```cpp
class TypeRegistry {
public:
    template <typename T> const TypeInfo* registerType(std::string_view name = {});

    const TypeInfo* find(std::type_index) const noexcept;
    const TypeInfo* find(std::string_view name) const noexcept;

    std::span<const TypeInfo* const> all() const noexcept;
    std::size_t size() const noexcept;

    static TypeRegistry& defaultInstance() noexcept;  // process-wide singleton
};
```

`registerType<T>()` uses `threadmaxx_reflect_fields(T*)` (via ADL).
Calling it twice with the same `T` returns the same `TypeInfo*` —
idempotent. The default instance is what the macro auto-registers to.

### 5.5 Visit / JSON (`visit.hpp`, `binders/json.hpp`)

```cpp
template <typename T, typename Visitor>
void visit_fields(T& obj, Visitor&& v);
// dispatches to the macro path if present, aggregate path otherwise.

// JSON binder — header-only consumer of TypeInfo:
std::string  to_json(const TypeInfo*, const void* obj);
ReflectResult<void> from_json(const TypeInfo*, void* obj, std::string_view);
```

Visitors implement `operator()(string_view name, T& value)` overload
sets; reflect's default-shipped `PrintVisitor`, `JsonVisitor`,
`HashVisitor` (FNV-1a-64 rollup), and `EqualsVisitor` cover the
common diagnostics needs.

### 5.6 Patch (`patch.hpp`)

```cpp
struct PatchEntry { std::string fieldPath; Value newValue; };
struct Patch      { std::vector<PatchEntry> entries; };

ReflectResult<void> applyPatch(const TypeInfo*, void* obj, const Patch&);
ReflectResult<Value> readField(const TypeInfo*, const void* obj,
                               std::string_view fieldPath);
```

### 5.7 Engine bridge (`engine_bridge.hpp`, opt-in)

```cpp
namespace threadmaxx::reflect::engine_bridge {

// Idempotent — safe to call multiple times.
void registerBuiltins(TypeRegistry& reg = TypeRegistry::defaultInstance());

template <typename T>
const TypeInfo* registerUserComponentType(
    TypeRegistry& reg = TypeRegistry::defaultInstance(),
    std::string_view name = {});

}
```

`registerBuiltins` registers every engine-side POD that reflect knows
about (`Transform`, `Velocity`, `Acceleration`, `Health`, `Faction`,
`Parent`, `MeshRef`, …). The list grows when new built-in components
land; reflect's R7 source is the authoritative manifest.

## 6. Implementation notes

### 6.1 Aggregate detection

C++20 structured bindings give us `auto& [a, b, c] = obj;` for
aggregates, but no built-in `field_count`. The trick (boost.pfr,
magic_get) is a constexpr probe: define a `UniversalInit` that's
implicitly convertible to anything, then test `T{UniversalInit{},
UniversalInit{}, ...}` for N from large to small; the largest N that
SFINAE-passes is the field count. Bounded to 32 fields in v1.0 —
that's well above every threadmaxx component (the largest is
`Health` with 3 fields, `Transform` with 4).

### 6.2 Macro details

`THREADMAXX_REFLECT(T, ...)` uses `__VA_OPT__` and a small recursion
to emit one `FieldDescriptor<T, &T::field, "field">` per name. Field
names go through the preprocessor as strings via `#field`. Compile
times stay low because all of the metadata is `constexpr` and lives
in the same TU as the type definition — the linker dedupes the
ADL hook across TUs.

### 6.3 Enum scan

For an enum `E`, scan `[EnumRange<E>::min, EnumRange<E>::max]`,
constructing each as `static_cast<E>(i)` and asking
`enum_name<static_cast<E>(i)>()` for the literal. The constexpr
function reads `__PRETTY_FUNCTION__` (clang/gcc) or
`__FUNCSIG__` (MSVC) and parses out the name between known delimiters.
A returned empty string means "no enumerator at that value" — skipped
in `enum_values()`.

### 6.4 Registry storage

`TypeRegistry` owns:
- a `std::deque<TypeInfo>` so pointers don't invalidate on growth,
- a `std::vector<FieldInfo>` per type owned by the same deque element,
- a `NameArena` (chained-slab string storage) for `std::string_view`
  names that originated at runtime,
- `std::unordered_map<std::type_index, TypeInfo*>` and
  `std::unordered_map<std::string_view, TypeInfo*>` indexes.

All four are guarded by one `std::shared_mutex`. `find` takes the
shared lock; `registerType` takes the unique lock. The hot-path
read after warmup is one shared lock + one unordered_map lookup —
no allocations.

### 6.5 Value SBO

```cpp
class Value {
    enum class Tag : std::uint8_t {
        Empty, Bool, I32, I64, U32, U64, F32, F64,
        String, OpaquePtr
    };
    Tag tag_;
    std::type_index typeIndex_{typeid(void)};
    union { ... 24-byte payload ... } storage_;
};
```

`sizeof(Value) <= 40`. Strings up to 23 chars stay in the SBO
slot; longer ones spill to heap. `OpaquePtr` carries the
`type_index` so visitors can look up `TypeInfo*` via the registry.

## 7. Suggested implementation order (v1.0)

- **R1 — Foundations:** library skeleton, version + types + config,
  no-engine-link gate, compile-time aggregate reflection
  (`field_count`, `get<I>`, `for_each_field` for aggregates).
- **R2 — Macro + named fields:** `THREADMAXX_REFLECT(T, …)` emits
  the ADL hook and `FieldList<T, …>` with names. `for_each_field`
  prefers the named path when present.
- **R3 — Runtime TypeInfo + TypeRegistry:** `TypeInfo`, `FieldInfo`,
  `TypeRegistry` with name + type_index indexes, default instance,
  template `registerType<T>()` that consumes the macro output.
- **R4 — Enum reflection:** `enum_name`, `enum_values`, `enum_cast`,
  bounded scan, customizable `EnumRange<E>`, flag-enum opt-in.
- **R5 — Attributes:** `Range`, `Tooltip`, `Hidden`, `Units`,
  `ReadOnly`, `THREADMAXX_REFLECT_WITH_ATTRS` macro form,
  `FieldInfo::attributes` projection.
- **R6 — Visitor + JSON binder:** `visit_fields(v, visitor)` traits,
  ship `PrintVisitor` / `HashVisitor` / `JsonVisitor` /
  `EqualsVisitor`; `to_json` / `from_json` driven by `TypeInfo`.
- **R7 — Engine bridge:** opt-in `engine_bridge.hpp` +
  `EngineBridge.cpp` register every built-in component;
  `UserComponent<T>` integration via `registerUserComponentType<T>()`.
- **R8 — Patch / apply:** `Value`, `Patch`, `PatchEntry`, path
  parser, `applyPatch` / `readField`.
- **v1.0 close-out:** version stamp, README + USER_GUIDE +
  MAINTAINER_GUIDE + CHANGELOG, crowd no-alloc gate (1000 lookups
  per frame, 0 allocs after warmup), `reflect_demo` headless that
  registers a struct, dumps JSON, applies a Patch, exits 0.

## 8. Open questions intentionally left for batches

- **Macro name collision with user code.** If a game already has a
  free function named `threadmaxx_reflect_fields`, our macro's ADL
  hook clashes. R2 will pick the final name; current candidate is
  `_threadmaxx_reflect_fields_v1` (underscore prefix marks it as
  ADL-only plumbing).
- **`std::vector<T>` element registration.** When the macro registers
  `struct Inventory { std::vector<Item> slots; }`, we want `Item` to
  also be visible to the editor. R3 will add a recursive
  registration pass: if `T` has a reflect hook, walk it; if `T` is
  arithmetic/string, leave it. Avoids the "user has to remember to
  also register `Item`" footgun.
- **Editor write-back from JSON.** `from_json` for an `OpaquePtr`
  field needs the registry to find the nested type. R6 will define
  the contract: the JSON must include `"$type": "name"` for any
  opaque field; mismatched names error rather than silently corrupt.
- **Hashing across runs.** `HashVisitor`'s FNV-1a-64 rollup is by
  field declaration order. Two runs of the same binary always
  produce the same hash for the same object; across binaries with
  different field orderings (someone reordered the POD), the hash
  diverges. That's the intended behavior — the hash is a content
  fingerprint, not a stable identity.

## 9. Versioning + ABI

Same shape as every other sibling library:

- `THREADMAXX_REFLECT_VERSION = MAJOR*10000 + MINOR*100 + PATCH`
  (currently `10000` for v1.0.0),
- `version_string() -> std::string_view` returns the human-readable
  `"1.0.0"`,
- `TypeInfo`, `FieldInfo`, `AttributeInfo` are PODs whose layout is
  pinned at v1.0.0 — appending fields is a minor bump, reordering
  or removing is a major bump.

The registry's name index uses string_view backed by registry-owned
storage; pointers into it are stable for the registry's lifetime.
Don't share `TypeInfo*` across `TypeRegistry` instances.

## 10. Test plan

R1+R2: compile-time `field_count<Aggregate>() == 3`,
`get<0>(obj) == obj.x`, `for_each_field` visits in declaration order.
R3: `registerType<Health>()` is idempotent, two finds return same
ptr, `find` by name and by `type_index` agree. R4: `enum_name(Foo::A)`
== `"A"`, `enum_cast<Foo>("A")` round-trips, flag-enum OR rendering.
R5: `Range{0,100}` projects into `FieldInfo::attributes`. R6:
`to_json` round-trip is byte-identical, `JsonVisitor` covers every
primitive. R7: every engine built-in has a `TypeInfo`, listed in the
expected order. R8: `applyPatch` mutates the right offset,
out-of-bounds vector index errors, unknown field errors.

v1.0 close-out: crowd no-alloc gate (1000 `find`+`readField` calls per
frame × 100 frames after warmup, 0 allocs / 0 frees), headless
`reflect_demo` exits 0.
