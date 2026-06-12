/// @file test_network_version.cpp
/// @brief Pin the threadmaxx_network version constants.

#include "Check.hpp"

#include <threadmaxx_network/version.hpp>

#include <string>

int main() {
    CHECK(std::string(threadmaxx::network::version_string()) == "1.0.0");
    CHECK_EQ(THREADMAXX_NETWORK_VERSION_MAJOR, 1);
    CHECK_EQ(THREADMAXX_NETWORK_VERSION_MINOR, 0);
    CHECK_EQ(THREADMAXX_NETWORK_VERSION_PATCH, 0);
    CHECK_EQ(THREADMAXX_NETWORK_VERSION, 1 * 10000 + 0 * 100 + 0);
    EXIT_WITH_RESULT();
}
