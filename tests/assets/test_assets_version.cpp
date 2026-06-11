#include "Check.hpp"

#include "threadmaxx_assets/version.hpp"

int main() {
    using namespace threadmaxx::assets;

    CHECK_EQ(version_string(), "0.1.0");
    CHECK_EQ(THREADMAXX_ASSETS_VERSION_MAJOR, 0);
    CHECK_EQ(THREADMAXX_ASSETS_VERSION_MINOR, 1);
    CHECK_EQ(THREADMAXX_ASSETS_VERSION_PATCH, 0);
    CHECK_EQ(THREADMAXX_ASSETS_VERSION, 100);

    EXIT_WITH_RESULT();
}
