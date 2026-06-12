/// @file test_reflect_patch_basic.cpp
/// @brief applyPatch writes a single field. readField round-trips.

#include "Check.hpp"

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/patch.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Health { int current; int max; float regen; };
THREADMAXX_REFLECT(Health, current, max, regen);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* h = reg.registerType<Health>("Health");

    Health body{50, 100, 1.0f};

    Patch p;
    p.entries.push_back({"current", Value::make<int>(75)});
    p.entries.push_back({"regen",   Value::make<float>(2.5f)});

    auto rc = applyPatch(h, &body, p);
    CHECK(rc.ok());
    CHECK_EQ(body.current, 75);
    CHECK_EQ(body.max, 100); // untouched
    CHECK_EQ(body.regen, 2.5f);

    // readField round-trip.
    auto rc2 = readField(h, &body, "max");
    CHECK(rc2.ok());
    int outMax = 0;
    CHECK(rc2.value.get(outMax));
    CHECK_EQ(outMax, 100);

    EXIT_WITH_RESULT();
}
