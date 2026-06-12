#include "Check.hpp"

#include "threadmaxx_assets/version.hpp"

int main() {
    using namespace threadmaxx::assets;

    CHECK_EQ(version_string(), "1.0.0");
    CHECK_EQ(THREADMAXX_ASSETS_VERSION_MAJOR, 1);
    CHECK_EQ(THREADMAXX_ASSETS_VERSION_MINOR, 0);
    CHECK_EQ(THREADMAXX_ASSETS_VERSION_PATCH, 0);
    CHECK_EQ(THREADMAXX_ASSETS_VERSION, 10000);

    EXIT_WITH_RESULT();
}
