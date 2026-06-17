// v1.3 — pin the v1.3 family.
//
// Verifies the current MINOR family:
//   1. MAJOR ≥ 1, MINOR ≥ 3 (compile-time).
//   2. Packed `THREADMAXX_VERSION` ≥ 10300.
//   3. `version_string()` starts with "1.3." (the v1.3.x family).
//
// Companion to `version_test.cpp` (loose v1.0 floor + packed-encoding
// self-consistency) and `version_test_v1_2.cpp` (relaxed v1.2 floor).
// Both run in the standard ctest sweep.
//
// When v1.4 ships, drop the family-string assertion here too (mirror
// the v1_2 relaxation pattern) and add `version_test_v1_4.cpp`.

#include "Check.hpp"

#include <threadmaxx/version.hpp>

#include <cstdio>
#include <cstring>

int main() {
    using namespace threadmaxx;

    // ---- 1. Macros at or past v1.3 -------------------------------------
    static_assert(THREADMAXX_VERSION_MAJOR >= 1,
        "library has reached the v1.0 floor");
    static_assert(THREADMAXX_VERSION_MAJOR > 1 || THREADMAXX_VERSION_MINOR >= 3,
        "library has reached the v1.3 floor");
    static_assert(THREADMAXX_VERSION >= 10300,
        "packed version has the v1.3 floor");

    // ---- 2. version_string is in the v1.3.x family ----------------------
    const char* v = version_string();
    CHECK(v != nullptr);
    CHECK(std::strncmp(v, "1.3.", 4) == 0);
    std::printf("[version_v1_3] %s (packed=%d)\n", v, THREADMAXX_VERSION);

    EXIT_WITH_RESULT();
}
