/// @file test_reflect_patch_unknown_field.cpp
/// @brief Unknown field name and type mismatch produce clean failures
/// with the right ErrorCode.

#include "Check.hpp"

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/patch.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Inventory { int gold; int gems; };
THREADMAXX_REFLECT(Inventory, gold, gems);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* inv = reg.registerType<Inventory>("Inventory");

    Inventory body{0, 0};

    // Unknown field.
    {
        Patch p;
        p.entries.push_back({"nope", Value::make<int>(1)});
        auto rc = applyPatch(inv, &body, p);
        CHECK(!rc.ok());
        CHECK(rc.code == ErrorCode::UnknownField);
    }

    // Type mismatch.
    {
        Patch p;
        p.entries.push_back({"gold", Value::make<float>(1.0f)});
        auto rc = applyPatch(inv, &body, p);
        CHECK(!rc.ok());
        CHECK(rc.code == ErrorCode::TypeMismatch);
    }

    // Nested path (v1.0 unsupported).
    {
        Patch p;
        p.entries.push_back({"sub.field", Value::make<int>(0)});
        auto rc = applyPatch(inv, &body, p);
        CHECK(!rc.ok());
        CHECK(rc.code == ErrorCode::Unsupported);
    }

    // readField with unknown name returns UnknownField.
    auto rd = readField(inv, &body, "nope");
    CHECK(!rd.ok());
    CHECK(rd.code == ErrorCode::UnknownField);

    EXIT_WITH_RESULT();
}
