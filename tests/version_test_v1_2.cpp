// Batch 33 / v1.2 — pin the v1.2 floor.
//
// Verifies the v1.2 floor:
//   1. MAJOR ≥ 1, MINOR ≥ 2 (compile-time).
//   2. Packed `THREADMAXX_VERSION` ≥ 10200.
//
// Companion to `version_test.cpp` (loose v1.0 floor + packed-encoding
// self-consistency) and `version_test_v1_3.cpp` (tight v1.3 family
// pin). Both run in the standard ctest sweep.
//
// History: when this file was written for v1.2.0 it ALSO asserted
// `version_string()` started with `"1.2."` — i.e. it was a v1.2.x
// FAMILY pin, not a floor pin. The family check was relaxed when
// v1.3.0 shipped so the test stays true as the floor rolls forward.
// The current MINOR's tight family check lives in
// `version_test_v1_<MINOR>.cpp`.

#include "Check.hpp"

#include <threadmaxx/version.hpp>

#include <cstdio>

int main() {
    using namespace threadmaxx;

    // ---- 1. Macros at or past v1.2 -------------------------------------
    static_assert(THREADMAXX_VERSION_MAJOR >= 1,
        "library has reached the v1.0 floor");
    static_assert(THREADMAXX_VERSION_MAJOR > 1 || THREADMAXX_VERSION_MINOR >= 2,
        "library has reached the v1.2 floor");
    static_assert(THREADMAXX_VERSION >= 10200,
        "packed version has the v1.2 floor");

    const char* v = version_string();
    CHECK(v != nullptr);
    std::printf("[version_v1_2_floor] %s (packed=%d)\n", v, THREADMAXX_VERSION);

    EXIT_WITH_RESULT();
}
