/// @file test_network_version.cpp
/// @brief Pin the threadmaxx_network version constants for the
/// pre-1.0 development series. Bumped to "1.0.0" / 10000 at close-out.

#include "Check.hpp"

#include <threadmaxx_network/version.hpp>

#include <string>

int main() {
    CHECK(std::string(threadmaxx::network::version_string()) == "0.9.0-dev");
    CHECK_EQ(THREADMAXX_NETWORK_VERSION_MAJOR, 0);
    CHECK_EQ(THREADMAXX_NETWORK_VERSION_MINOR, 9);
    CHECK_EQ(THREADMAXX_NETWORK_VERSION_PATCH, 0);
    CHECK_EQ(THREADMAXX_NETWORK_VERSION, 0 * 10000 + 9 * 100 + 0);
    EXIT_WITH_RESULT();
}
