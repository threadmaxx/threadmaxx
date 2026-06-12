/// @file test_reflect_aggregate_for_each.cpp
/// @brief `for_each_field(obj, fn)` walks every field in declaration
/// order. The fallback path supplies `(index, value)` since no macro
/// has been applied.

#include "Check.hpp"

#include <vector>

#include <threadmaxx_reflect/aggregate.hpp>

namespace {

struct Pos { float x; float y; float z; };

struct Mixed {
    int      hp;
    float    speed;
    double   stamina;
    int      armor;
};

} // namespace

int main() {
    using namespace threadmaxx::reflect;

    {
        Pos p{1.0f, 2.0f, 3.0f};
        std::vector<float> seen;
        for_each_field(p, [&](std::size_t /*i*/, auto& v) {
            seen.push_back(static_cast<float>(v));
        });
        CHECK_EQ(seen.size(), 3u);
        CHECK_EQ(seen[0], 1.0f);
        CHECK_EQ(seen[1], 2.0f);
        CHECK_EQ(seen[2], 3.0f);
    }

    {
        Mixed m{42, 3.5f, 7.0, 11};
        std::vector<std::size_t> indices;
        std::vector<double>       values;
        for_each_field(m, [&](std::size_t i, auto& v) {
            indices.push_back(i);
            values.push_back(static_cast<double>(v));
        });
        CHECK_EQ(indices.size(), 4u);
        CHECK_EQ(indices[0], 0u);
        CHECK_EQ(indices[1], 1u);
        CHECK_EQ(indices[2], 2u);
        CHECK_EQ(indices[3], 3u);
        CHECK_EQ(values[0], 42.0);
        CHECK_EQ(values[1], 3.5);
        CHECK_EQ(values[2], 7.0);
        CHECK_EQ(values[3], 11.0);
    }

    // get<I>(obj) returns by reference; mutations stick.
    {
        Pos p{0, 0, 0};
        get<0>(p) = 10.0f;
        get<2>(p) = 30.0f;
        CHECK_EQ(p.x, 10.0f);
        CHECK_EQ(p.y, 0.0f);
        CHECK_EQ(p.z, 30.0f);
    }

    EXIT_WITH_RESULT();
}
