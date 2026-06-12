/// @file test_reflect_registry_lookup_by_type_index.cpp
/// @brief `find(type_index)` agrees with `find(name)`. The same
/// `TypeInfo*` returns from both lookup paths, AND the FieldInfo span
/// has the expected size + ordering.

#include "Check.hpp"

#include <typeindex>

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Health { int current; int max; float regen; };
THREADMAXX_REFLECT(Health, current, max, regen);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* h = reg.registerType<Health>("Health");
    CHECK(h != nullptr);

    auto* byIdx = reg.find(std::type_index(typeid(Health)));
    CHECK_EQ(byIdx, h);
    CHECK_EQ(reg.find("Health"), h);

    CHECK_EQ(h->fields.size(), 3u);
    CHECK_EQ(h->fields[0].name, std::string_view{"current"});
    CHECK_EQ(h->fields[1].name, std::string_view{"max"});
    CHECK_EQ(h->fields[2].name, std::string_view{"regen"});

    CHECK_EQ(h->fields[0].typeIndex, std::type_index(typeid(int)));
    CHECK_EQ(h->fields[1].typeIndex, std::type_index(typeid(int)));
    CHECK_EQ(h->fields[2].typeIndex, std::type_index(typeid(float)));

    CHECK_EQ(h->fields[0].typeName, std::string_view{"int32"});
    CHECK_EQ(h->fields[2].typeName, std::string_view{"float"});

    // Offset / size checks against offsetof.
    CHECK_EQ(h->fields[0].offset, offsetof(Health, current));
    CHECK_EQ(h->fields[1].offset, offsetof(Health, max));
    CHECK_EQ(h->fields[2].offset, offsetof(Health, regen));

    // Typed get / set.
    Health body{1, 2, 3.0f};
    auto* cur = h->fields[0].get<int>(&body);
    CHECK(cur != nullptr);
    CHECK_EQ(*cur, 1);
    CHECK(h->fields[0].get<float>(&body) == nullptr);   // type mismatch

    CHECK(h->fields[2].set<float>(&body, 9.5f));
    CHECK_EQ(body.regen, 9.5f);
    CHECK(!h->fields[2].set<int>(&body, 7));            // type mismatch

    // findField round-trip.
    auto* field = h->findField("max");
    CHECK(field != nullptr);
    CHECK_EQ(field->offset, offsetof(Health, max));
    CHECK(h->findField("nope") == nullptr);

    EXIT_WITH_RESULT();
}
