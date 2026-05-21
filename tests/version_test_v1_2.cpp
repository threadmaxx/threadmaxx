// Batch 33 / v1.2 — pin the library version to 1.2.0.
//
// Verifies the v1.2 floor:
//   1. MAJOR ≥ 1, MINOR ≥ 2 (compile-time).
//   2. Packed `THREADMAXX_VERSION` ≥ 10200.
//   3. `version_string()` starts with "1.2." (the v1.2.x family).
//
// Companion to `version_test.cpp`, which holds the looser v1.0 floor
// and the packed-encoding self-consistency check. Both run in the
// standard ctest sweep.

#include "Check.hpp"

#include <threadmaxx/version.hpp>

#include <cstdio>
#include <cstring>

int main() {
    using namespace threadmaxx;

    // ---- 1. Macros at or past v1.2 -------------------------------------
    static_assert(THREADMAXX_VERSION_MAJOR >= 1,
        "library has reached the v1.0 floor");
    static_assert(THREADMAXX_VERSION_MAJOR > 1 || THREADMAXX_VERSION_MINOR >= 2,
        "library has reached the v1.2 floor");
    static_assert(THREADMAXX_VERSION >= 10200,
        "packed version has the v1.2 floor");

    // ---- 2. version_string is in the v1.2.x family ----------------------
    const char* v = version_string();
    CHECK(v != nullptr);
    // Accept "1.2.x" — the family identifier is the leading "1.2.".
    CHECK(std::strncmp(v, "1.2.", 4) == 0);
    std::printf("[version_v1_2] %s (packed=%d)\n", v, THREADMAXX_VERSION);

    EXIT_WITH_RESULT();
}
