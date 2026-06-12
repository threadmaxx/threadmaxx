/// @file test_reflect_version.cpp
/// @brief Pin the version-string + integer values for the active
/// pre-1.0 development series. The close-out batch flips both to
/// "1.0.0" / 10000.

#include "Check.hpp"

#include <string>

#include <threadmaxx_reflect/version.hpp>

int main() {
    CHECK_EQ(std::string(threadmaxx::reflect::version_string()),
             std::string("1.0.0"));
    CHECK_EQ(THREADMAXX_REFLECT_VERSION_MAJOR, 1);
    CHECK_EQ(THREADMAXX_REFLECT_VERSION_MINOR, 0);
    CHECK_EQ(THREADMAXX_REFLECT_VERSION_PATCH, 0);
    CHECK_EQ(THREADMAXX_REFLECT_VERSION, 1 * 10000 + 0 * 100 + 0);
    EXIT_WITH_RESULT();
}
