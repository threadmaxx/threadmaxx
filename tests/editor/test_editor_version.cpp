/// @file test_editor_version.cpp
/// @brief Pin the threadmaxx_editor version constants.

#include "Check.hpp"

#include <threadmaxx_editor/version.hpp>

#include <string>

int main() {
    CHECK(std::string(threadmaxx::editor::version_string()) == "1.0.0");
    CHECK_EQ(THREADMAXX_EDITOR_VERSION_MAJOR, 1);
    CHECK_EQ(THREADMAXX_EDITOR_VERSION_MINOR, 0);
    CHECK_EQ(THREADMAXX_EDITOR_VERSION_PATCH, 0);
    CHECK_EQ(THREADMAXX_EDITOR_VERSION, 1 * 10000 + 0 * 100 + 0);
    EXIT_WITH_RESULT();
}
