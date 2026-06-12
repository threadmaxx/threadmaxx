/// @file test_reflect_json_round_trip.cpp
/// @brief `to_json(typeInfo, obj)` -> `from_json(typeInfo, obj, str)`
/// is a faithful round-trip on primitives.

#include "Check.hpp"

#include <string>

#include <threadmaxx_reflect/binders/json.hpp>
#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {
struct Health { int current; int max; float regen; bool alive; };
THREADMAXX_REFLECT(Health, current, max, regen, alive);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    TypeRegistry reg;
    auto* h = reg.registerType<Health>("Health");

    Health body{42, 100, 1.5f, true};
    std::string j = to_json(h, &body);
    // Sanity: every field name present.
    CHECK(j.find("\"current\":42") != std::string::npos);
    CHECK(j.find("\"max\":100") != std::string::npos);
    CHECK(j.find("\"regen\":") != std::string::npos);
    CHECK(j.find("\"alive\":true") != std::string::npos);

    // Round-trip parse into a fresh body.
    Health restored{};
    auto rc = from_json(h, &restored, j);
    CHECK(rc.ok());
    CHECK_EQ(restored.current, 42);
    CHECK_EQ(restored.max, 100);
    CHECK_EQ(restored.regen, 1.5f);
    CHECK_EQ(restored.alive, true);

    // Unknown field is silently skipped.
    Health body2{1, 2, 0.5f, false};
    auto rc2 = from_json(h, &body2, R"({"unknown":99,"current":7})");
    CHECK(rc2.ok());
    CHECK_EQ(body2.current, 7);
    CHECK_EQ(body2.max, 2);  // unchanged

    // Malformed JSON returns ParseError.
    Health body3{};
    auto rc3 = from_json(h, &body3, "not json");
    CHECK(!rc3.ok());
    CHECK(rc3.code == ErrorCode::ParseError);

    // Empty object is a success no-op.
    Health body4{5, 10, 0.1f, true};
    auto rc4 = from_json(h, &body4, "{}");
    CHECK(rc4.ok());
    CHECK_EQ(body4.current, 5);

    EXIT_WITH_RESULT();
}
