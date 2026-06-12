/// @file test_reflect_macro_named_for_each.cpp
/// @brief Named for_each visits in declaration order; the FieldList
/// preserves the order the macro received.

#include "Check.hpp"

#include <string>
#include <vector>

#include <threadmaxx_reflect/aggregate.hpp>
#include <threadmaxx_reflect/macro.hpp>

namespace {
struct Transform {
    float x;
    float y;
    float z;
    float rotation;
};
THREADMAXX_REFLECT(Transform, x, y, z, rotation);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    Transform t{1.0f, 2.0f, 3.0f, 4.5f};
    std::vector<std::string> names;
    std::vector<float>       values;
    for_each_field(t, [&](std::string_view name, auto& v) {
        names.emplace_back(name);
        values.push_back(static_cast<float>(v));
    });

    CHECK_EQ(names.size(), 4u);
    CHECK_EQ(names[0], "x");
    CHECK_EQ(names[1], "y");
    CHECK_EQ(names[2], "z");
    CHECK_EQ(names[3], "rotation");
    CHECK_EQ(values[0], 1.0f);
    CHECK_EQ(values[1], 2.0f);
    CHECK_EQ(values[2], 3.0f);
    CHECK_EQ(values[3], 4.5f);

    EXIT_WITH_RESULT();
}
