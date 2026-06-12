/// @file test_reflect_macro_offsets.cpp
/// @brief FieldDescriptor::offset() / sizeBytes() / alignBytes()
/// match offsetof / sizeof / alignof.

#include "Check.hpp"

#include <cstddef>

#include <threadmaxx_reflect/macro.hpp>

namespace {
struct PhysicsBody {
    float    mass;          // offset 0
    double   inertia;       // offset 8 (after padding)
    int      collisionMask; // offset 16
    bool     isStatic;      // offset 20
};
THREADMAXX_REFLECT(PhysicsBody, mass, inertia, collisionMask, isStatic);
} // namespace

int main() {
    using namespace threadmaxx::reflect;
    using FL = decltype(_threadmaxx_reflect_fields_v1(static_cast<PhysicsBody*>(nullptr)));
    static_assert(FL::size == 4);

    // Walk via for_each over a temp and verify offset symmetry by
    // comparing addresses.
    PhysicsBody body{};
    auto* base = reinterpret_cast<std::byte*>(&body);

    bool sawMass = false, sawInertia = false, sawMask = false, sawStatic = false;
    FL::for_each(body, [&](std::string_view name, auto& v) {
        auto* addr = reinterpret_cast<std::byte*>(&v);
        std::size_t observed = static_cast<std::size_t>(addr - base);
        if (name == "mass") {
            CHECK_EQ(observed, offsetof(PhysicsBody, mass));
            sawMass = true;
        } else if (name == "inertia") {
            CHECK_EQ(observed, offsetof(PhysicsBody, inertia));
            sawInertia = true;
        } else if (name == "collisionMask") {
            CHECK_EQ(observed, offsetof(PhysicsBody, collisionMask));
            sawMask = true;
        } else if (name == "isStatic") {
            CHECK_EQ(observed, offsetof(PhysicsBody, isStatic));
            sawStatic = true;
        }
    });
    CHECK(sawMass && sawInertia && sawMask && sawStatic);

    EXIT_WITH_RESULT();
}
