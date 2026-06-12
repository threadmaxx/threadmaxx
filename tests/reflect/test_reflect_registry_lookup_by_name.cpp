/// @file test_reflect_registry_lookup_by_name.cpp
/// @brief `find(name)` matches the registered friendly name; missing
/// name returns nullptr.

#include "Check.hpp"

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Velocity { float vx; float vy; float vz; };
THREADMAXX_REFLECT(Velocity, vx, vy, vz);

struct Acceleration { float ax; float ay; float az; };
THREADMAXX_REFLECT(Acceleration, ax, ay, az);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* v = reg.registerType<Velocity>("Velocity");
    auto* a = reg.registerType<Acceleration>("Acceleration");

    CHECK_EQ(reg.find("Velocity"), v);
    CHECK_EQ(reg.find("Acceleration"), a);
    CHECK(reg.find("nope") == nullptr);
    CHECK(reg.find("velocity") == nullptr); // case-sensitive

    CHECK_EQ(reg.size(), 2u);

    EXIT_WITH_RESULT();
}
