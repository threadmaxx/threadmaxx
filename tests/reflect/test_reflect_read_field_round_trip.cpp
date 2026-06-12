/// @file test_reflect_read_field_round_trip.cpp
/// @brief readField -> applyPatch round-trip is faithful for every
/// supported primitive.

#include "Check.hpp"

#include <cstdint>

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/patch.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Mixed {
    bool          b;
    std::int32_t  i32;
    std::uint64_t u64;
    float         f;
    double        d;
};
THREADMAXX_REFLECT(Mixed, b, i32, u64, f, d);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* m = reg.registerType<Mixed>("Mixed");

    Mixed body{true, 42, 0xCAFEBABEull, 3.5f, 7.25};
    Mixed restored{};

    // For each field, read it from `body` then apply to `restored`.
    Patch p;
    for (const char* name : {"b", "i32", "u64", "f", "d"}) {
        auto r = readField(m, &body, name);
        CHECK(r.ok());
        p.entries.push_back({name, r.value});
    }

    auto rc = applyPatch(m, &restored, p);
    CHECK(rc.ok());

    CHECK_EQ(restored.b, body.b);
    CHECK_EQ(restored.i32, body.i32);
    CHECK_EQ(restored.u64, body.u64);
    CHECK_EQ(restored.f, body.f);
    CHECK_EQ(restored.d, body.d);

    EXIT_WITH_RESULT();
}
