/// @file test_reflect_attr_query.cpp
/// @brief Foreign `TypeInfo*` (from another registry) doesn't poison
/// the addFieldAttribute path — returns false cleanly.

#include "Check.hpp"

#include <threadmaxx_reflect/attributes.hpp>
#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Inventory { int gold; int gems; };
THREADMAXX_REFLECT(Inventory, gold, gems);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry regA;
    TypeRegistry regB;

    auto* a = regA.registerType<Inventory>("Inventory");
    auto* b = regB.registerType<Inventory>("Inventory");
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    CHECK(a != b); // distinct registries, distinct TypeInfo*

    // Adding a regA TypeInfo* to regB returns false.
    CHECK(!regB.addFieldAttribute(a, "gold", Range{0.0, 1000.0}));
    // Nullptr -> false.
    CHECK(!regA.addFieldAttribute(static_cast<const TypeInfo*>(nullptr),
                                   "gold", Range{0.0, 1.0}));

    // Correct registry succeeds.
    CHECK(regA.addFieldAttribute(a, "gold", Range{0.0, 1000.0}));
    CHECK_EQ(a->findField("gold")->attributes.size(), 1u);

    // regB's field stays untouched.
    CHECK_EQ(b->findField("gold")->attributes.size(), 0u);

    EXIT_WITH_RESULT();
}
