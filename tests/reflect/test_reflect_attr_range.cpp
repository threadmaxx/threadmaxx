/// @file test_reflect_attr_range.cpp
/// @brief `Range` attribute attaches to a registered field and shows
/// up in `FieldInfo::attributes`.

#include "Check.hpp"

#include <threadmaxx_reflect/attributes.hpp>
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

    CHECK(reg.addFieldAttribute(h, "current", Range{0.0, 100.0}));
    CHECK(reg.addFieldAttribute(h, "max", Range{1.0, 999.0}));

    // Unknown field -> false.
    CHECK(!reg.addFieldAttribute(h, "nope", Range{0.0, 1.0}));

    // current has one attribute now.
    const auto* current = h->findField("current");
    CHECK(current != nullptr);
    CHECK_EQ(current->attributes.size(), 1u);
    CHECK_EQ(current->attributes[0].name, std::string_view{"Range"});
    CHECK_EQ(current->attributes[0].payload, std::string_view{"0,100"});

    const auto* max = h->findField("max");
    CHECK_EQ(max->attributes.size(), 1u);
    CHECK_EQ(max->attributes[0].payload, std::string_view{"1,999"});

    // regen has no attributes attached.
    const auto* regen = h->findField("regen");
    CHECK_EQ(regen->attributes.size(), 0u);

    EXIT_WITH_RESULT();
}
