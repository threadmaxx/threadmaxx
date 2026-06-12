# `threadmaxx_reflect` — v1.0.0

Compile-time + runtime reflection for projects built on `threadmaxx`.
The introspection layer for editors, scripting bindings, debug
overlays, and serialization paths that need to walk POD components
without hand-rolling per-type code.

## Quickstart

```cpp
#include <threadmaxx_reflect/threadmaxx_reflect.hpp>

namespace game {
struct Health { int current; int max; float regen; };
THREADMAXX_REFLECT(Health, current, max, regen);
} // namespace game

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* h = reg.registerType<game::Health>("Health");
    reg.addFieldAttribute(h, "current", Range{0.0, 100.0});

    game::Health body{42, 100, 1.5f};
    std::string j = to_json(h, &body);   // {"current":42,"max":100,"regen":1.5,...}

    // Or visit at compile time:
    for_each_field(body, [](std::string_view name, auto& v) {
        std::printf("%s = ...\n", std::string(name).c_str());
    });
}
```

## Pillars

| Pillar | What you get |
|---|---|
| **Compile-time aggregates** (R1) | `field_count<T>`, `get<I>`, `for_each_field` — zero runtime cost, works without macros |
| **Macro registration** (R2) | `THREADMAXX_REFLECT(T, …)` emits an ADL hook with named field metadata |
| **Runtime TypeRegistry** (R3) | `shared_mutex`-locked, deque-backed; pointers stable for the registry's lifetime |
| **Enum reflection** (R4) | `enum_name`, `enum_values`, `enum_cast`; flag-enum opt-in |
| **Attributes** (R5) | `Range`, `Tooltip`, `Hidden`, `Units`, `ReadOnly`, `Step` |
| **Visitors + JSON** (R6) | `PrintVisitor`, `HashVisitor`, `EqualsVisitor`, `to_json`, `from_json` |
| **Engine bridge** (R7) | `registerBuiltins()` lights up every engine component in one call |
| **Patch / readField** (R8) | Type-erased `Value` + `Patch` for editor / network mutation |

## Build

Opt-in via the root option (default ON):

```bash
cmake -S . -B build -DTHREADMAXX_BUILD_REFLECT=ON
```

Engine bridge auto-enables when `threadmaxx::threadmaxx` is in the
build tree; exports `THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1` so
consumers can `#if`-gate.

## Performance gates

- **Crowd no-alloc gate** — 1000 `find` + `readField` + `enum_cast` calls
  per frame × 100 frames after a 5-frame warmup: **0 allocations, 0 frees**.
- **Hot-path lookup** — one `shared_mutex` read lock + one
  `unordered_map` find. No allocations after startup registration.
- **Macro path** — `field_count<T>` / `get<I>(t)` / `for_each_field`
  resolve at constexpr; runtime cost is the visitor body alone.

## What's deferred to v1.x

- Container reflection (`std::map`, `std::unordered_set`, intrusive lists).
- Nested aggregate JSON binding (Vec3-inside-Transform currently
  renders as `null` in `to_json`; the editor can drill in via the
  registry's `find(typeIndex)`).
- `THREADMAXX_REFLECT_WITH_ATTRS` compile-time attribute macro
  (runtime `addFieldAttribute` ships today).
- Cross-process stable type IDs (today the binding is the name string).
- Path-style patches (`Transform.position.x`); v1.0 supports flat names.

See `FUTURE_WORK.md` for the full deferred list.

## Related

- `DESIGN_NOTES.md` — design rationale, principles, and the API spec.
- `USER_GUIDE.md` — step-by-step walkthrough of the eight pillars.
- `MAINTAINER_GUIDE.md` — adding a new attribute / built-in / binder,
  ABI rules, hot-path discipline.
- `CHANGELOG.md` — versioned release notes.
