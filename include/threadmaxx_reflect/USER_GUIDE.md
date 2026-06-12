# `threadmaxx_reflect` — user guide

A step-by-step walkthrough of every public surface. Code samples
compile against v1.0.0; replace `game::` with your namespace.

## 1. Compile-time aggregate reflection

When you don't need names, the aggregate path works on any POD:

```cpp
#include <threadmaxx_reflect/aggregate.hpp>

struct Pos { float x, y, z; };

static_assert(threadmaxx::reflect::field_count<Pos>() == 3);

Pos p{1, 2, 3};
threadmaxx::reflect::for_each_field(p, [](std::size_t i, auto& v) {
    // i = 0, 1, 2; v = x, y, z by reference
});
```

`get<I>(obj)` returns the I-th field by reference; mutations stick.

## 2. Macro registration

For named fields and runtime introspection:

```cpp
#include <threadmaxx_reflect/macro.hpp>

namespace game {
struct Health { int current; int max; float regen; };
THREADMAXX_REFLECT(Health, current, max, regen);
} // namespace game
```

**Placement rule:** the `THREADMAXX_REFLECT` line must be in the SAME
namespace as the type definition. ADL needs the hook visible in the
type's namespace.

Then `for_each_field` dispatches to the named path:

```cpp
game::Health h{42, 100, 1.5f};
threadmaxx::reflect::for_each_field(h, [](std::string_view name, auto& v) {
    // name = "current", "max", "regen"
});
```

Up to 16 fields per type. Adding 17+ is a v1.x extension.

## 3. Runtime TypeRegistry

For editor / scripting / serializer paths that walk types they didn't
see at compile time:

```cpp
#include <threadmaxx_reflect/registry.hpp>

threadmaxx::reflect::TypeRegistry reg;
auto* info = reg.registerType<game::Health>("Health"); // idempotent

const auto* field = info->findField("current");
// field->name == "current", field->offset, field->typeIndex, …
```

Lookup by name and by `type_index`:

```cpp
auto* a = reg.find("Health");
auto* b = reg.find(std::type_index(typeid(game::Health)));
assert(a == b);
```

Pointers are stable for the registry's lifetime. Process-wide default
instance: `TypeRegistry::defaultInstance()`.

## 4. Enum reflection

```cpp
#include <threadmaxx_reflect/enum.hpp>

enum class Color { Red, Green, Blue };

constexpr auto name = threadmaxx::reflect::enum_name<Color::Red>();  // "Red"
auto value = threadmaxx::reflect::enum_cast<Color>("Blue");          // Blue
auto pairs = threadmaxx::reflect::enum_values<Color>();              // span<{Color, sv}>
```

Customize the scan range for enums with values outside [-128, 128]:

```cpp
template <>
struct threadmaxx::reflect::EnumRange<MyEnum> {
    static constexpr int  min    = 0;
    static constexpr int  max    = 1024;
    static constexpr bool isFlag = false;  // true for OR-able bit flags
};
```

Flag enums get `|`-separated parsing in `enum_cast`:
`enum_cast<Mask>("Player|Enemy")`.

## 5. Attributes

Editor / tooling hints attached at runtime:

```cpp
#include <threadmaxx_reflect/attributes.hpp>

reg.addFieldAttribute(info, "current",  Range{0.0, 100.0});
reg.addFieldAttribute(info, "current",  Tooltip{"current HP"});
reg.addFieldAttribute(info, "regen",    Units{"hp/s"});
reg.addFieldAttribute(info, "max",      ReadOnly{});
```

Read them through `FieldInfo::attributes`:

```cpp
for (const auto& attr : info->findField("current")->attributes) {
    // attr.name = "Range", "Tooltip", …
    // attr.payload = "0,100", "current HP", …
}
```

Ships: `Range`, `Tooltip`, `Hidden`, `Units`, `ReadOnly`, `Step`.

## 6. Visitors + JSON

Stringify:

```cpp
#include <threadmaxx_reflect/visit.hpp>

std::string buf;
visit_fields(h, PrintVisitor(buf));
// buf == "current=42, max=100, regen=1.5"
```

Hash:

```cpp
HashVisitor v;
visit_fields(h, v);
std::uint64_t fingerprint = v.hash();
```

Equals (via the helper):

```cpp
bool same = fields_equal(a, b);  // byte-equal at the field level
```

JSON round-trip (runtime-driven):

```cpp
#include <threadmaxx_reflect/binders/json.hpp>

std::string j = to_json(info, &h);
// {"current":42,"max":100,"regen":1.5}

game::Health restored{};
auto rc = from_json(info, &restored, j);
assert(rc.ok());
```

## 7. Engine bridge

Light up every engine built-in in one call:

```cpp
#include <threadmaxx_reflect/engine_bridge.hpp>

threadmaxx::reflect::engine_bridge::registerBuiltins(reg);
// Registers Vec3, Quat, Transform, Velocity, Acceleration, RenderTag,
// UserData, Parent, Health, Faction, AnimationStateRef,
// PhysicsBodyRef, NavAgentRef, BoundingVolume — 14 types.
```

User components alongside:

```cpp
namespace game { struct CubeRender { int meshId; int colorIdx; }; }
THREADMAXX_REFLECT(game::CubeRender, meshId, colorIdx);

threadmaxx::reflect::engine_bridge::registerUserComponentType<game::CubeRender>(
    reg, "CubeRender");
```

Bridge auto-enables when `threadmaxx::threadmaxx` is in the build;
`#ifdef THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE` to gate downstream code.

## 8. Patch / readField

Type-erased mutation for editor / network paths:

```cpp
#include <threadmaxx_reflect/patch.hpp>

Patch p;
p.entries.push_back({"current", Value::make<int>(75)});
p.entries.push_back({"regen",   Value::make<float>(2.5f)});

auto rc = applyPatch(info, &h, p);
assert(rc.ok());

// Reverse: read a field back out as a Value.
auto rd = readField(info, &h, "max");
int outMax = 0;
rd.value.get(outMax);  // == 100
```

`Value` is a 40-byte SBO holder; supports every arithmetic primitive
plus `bool`. Type mismatch returns `false` from `get` (no UB).

v1.0 supports flat field names only; nested paths
(`Transform.position.x`) are a v1.x feature.
