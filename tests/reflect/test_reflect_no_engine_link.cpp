/// @file test_reflect_no_engine_link.cpp
/// @brief Sanity that threadmaxx_reflect compiles without pulling in
/// the engine. If this TU references no `threadmaxx::Engine` symbol,
/// the link succeeds with only `threadmaxx::reflect`.

#include "Check.hpp"

#include <threadmaxx_reflect/threadmaxx_reflect.hpp>

int main() {
    constexpr auto v = threadmaxx::reflect::version_string();
    CHECK(!v.empty());
    EXIT_WITH_RESULT();
}
