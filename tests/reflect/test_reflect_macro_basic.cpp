/// @file test_reflect_macro_basic.cpp
/// @brief THREADMAXX_REFLECT emits an ADL hook returning a FieldList
/// with the expected size and per-field names.

#include "Check.hpp"

#include <string>

#include <threadmaxx_reflect/aggregate.hpp>
#include <threadmaxx_reflect/macro.hpp>

namespace {
struct Health {
    int   current;
    int   max;
    float regen;
};
THREADMAXX_REFLECT(Health, current, max, regen);
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    // ADL probe finds the hook.
    static_assert(detail::HasReflectHook<Health>);

    using FL = decltype(_threadmaxx_reflect_fields_v1(static_cast<Health*>(nullptr)));
    static_assert(FL::size == 3);

    Health h{42, 100, 1.5f};

    // for_each_field dispatches to the macro path -> (string_view, value).
    std::string acc;
    for_each_field(h, [&](std::string_view name, auto& v) {
        acc += std::string(name);
        acc += "=";
        acc += std::to_string(static_cast<double>(v)).substr(0, 4);
        acc += ";";
    });
    CHECK(acc.find("current=") != std::string::npos);
    CHECK(acc.find("max=") != std::string::npos);
    CHECK(acc.find("regen=") != std::string::npos);

    // Field references are mutable.
    for_each_field(h, [&](std::string_view name, auto& v) {
        if (name == "current") {
            using V = std::remove_reference_t<decltype(v)>;
            if constexpr (std::is_same_v<V, int>) v = 999;
        }
    });
    CHECK_EQ(h.current, 999);

    EXIT_WITH_RESULT();
}
