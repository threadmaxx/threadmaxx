/// @file test_reflect_registry_idempotent.cpp
/// @brief `registerType<T>()` called multiple times for the same `T`
/// returns the same `TypeInfo*`. Pointer stability is the contract.

#include "Check.hpp"

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Vec3 { float x; float y; float z; };
THREADMAXX_REFLECT(Vec3, x, y, z);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* a = reg.registerType<Vec3>("Vec3");
    auto* b = reg.registerType<Vec3>("Vec3");
    auto* c = reg.registerType<Vec3>("ignored-name");

    CHECK(a != nullptr);
    CHECK(b == a);
    CHECK(c == a); // already registered -> name override on second call is ignored

    CHECK_EQ(reg.size(), 1u);

    EXIT_WITH_RESULT();
}
