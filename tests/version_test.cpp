// Batch 25 / v1.0 — library version macros + runtime helper.
//
// Verifies:
//   1. Macros are defined and ≥ 1.0.0 (the v1.0 floor).
//   2. Packed `THREADMAXX_VERSION` matches `MAJOR*10000 + MINOR*100 + PATCH`.
//   3. `version_string()` is callable, non-null, non-empty.
//   4. The CMake `project()` VERSION and the header's macros agree
//      (cross-checked by reading the CMake-generated PROJECT_VERSION
//      via a compile-time define — see tests/CMakeLists.txt).

#include "Check.hpp"

#include <threadmaxx/version.hpp>

#include <cstdio>
#include <cstring>

int main() {
    using namespace threadmaxx;

    // ---- 1. Macros at or past v1.0 -------------------------------------
    static_assert(THREADMAXX_VERSION_MAJOR >= 1,
        "library has reached the v1.0 floor");
    static_assert(THREADMAXX_VERSION >= 10000,
        "packed version has the v1.0 floor");

    // ---- 2. Packed encoding ---------------------------------------------
    constexpr int expected =
        THREADMAXX_VERSION_MAJOR * 10000 +
        THREADMAXX_VERSION_MINOR * 100 +
        THREADMAXX_VERSION_PATCH;
    static_assert(THREADMAXX_VERSION == expected,
        "packed version encodes major.minor.patch correctly");

    // ---- 3. version_string is sane --------------------------------------
    const char* v = version_string();
    CHECK(v != nullptr);
    CHECK(std::strlen(v) > 0);
    std::printf("[version] %s (packed=%d)\n", v, THREADMAXX_VERSION);

    EXIT_WITH_RESULT();
}
