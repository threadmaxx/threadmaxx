/// @file test_reflect_aggregate_count.cpp
/// @brief `field_count<T>()` for plain aggregates returns the right
/// number of fields. All compile-time — every CHECK_EQ resolves at
/// constexpr.

#include "Check.hpp"

#include <threadmaxx_reflect/aggregate.hpp>

namespace {

struct Empty {};
struct One { int a; };
struct Two { int a; float b; };
struct Three { int a; float b; double c; };
struct Six { int a, b, c, d, e, f; };
struct VecLike { float x, y, z; };

} // namespace

int main() {
    using namespace threadmaxx::reflect;

    CHECK_EQ(field_count<Empty>(), 0u);
    CHECK_EQ(field_count<One>(), 1u);
    CHECK_EQ(field_count<Two>(), 2u);
    CHECK_EQ(field_count<Three>(), 3u);
    CHECK_EQ(field_count<Six>(), 6u);
    CHECK_EQ(field_count<VecLike>(), 3u);

    // CV / reference qualifiers shouldn't affect the count.
    CHECK_EQ(field_count<const Three>(), 3u);
    CHECK_EQ(field_count<Three&>(), 3u);

    EXIT_WITH_RESULT();
}
