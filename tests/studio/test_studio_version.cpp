/// @file test_studio_version.cpp
/// @brief Pin the threadmaxx_studio version constants.

#include "Check.hpp"

#include <threadmaxx_studio/version.hpp>

#include <string>

int main() {
    CHECK(std::string(threadmaxx::studio::version_string()) == "0.1.0-dev");
    CHECK_EQ(THREADMAXX_STUDIO_VERSION_MAJOR, 0);
    CHECK_EQ(THREADMAXX_STUDIO_VERSION_MINOR, 1);
    CHECK_EQ(THREADMAXX_STUDIO_VERSION_PATCH, 0);
    CHECK_EQ(THREADMAXX_STUDIO_VERSION, 0 * 10000 + 1 * 100 + 0);
    EXIT_WITH_RESULT();
}
