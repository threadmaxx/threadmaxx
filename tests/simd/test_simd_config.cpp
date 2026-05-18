// threadmaxx_simd — config + capabilities tests.
//
// Verifies:
//   1. `compile_time_capabilities()` is constexpr and returns scalar==true.
//   2. `runtime_capabilities()` agrees with `compile_time_capabilities()`
//      on every flag (in S1; S5 will introduce dynamic downgrades).
//   3. `preferred_isa()` returns a value consistent with the
//      capability set (never returns an ISA the build wasn't told to
//      enable).
//   4. The ISA enum has the four expected members (compile-time check).

#include "Check.hpp"

#include <threadmaxx_simd/config.hpp>
#include <threadmaxx_simd/cpu.hpp>
#include <threadmaxx_simd/version.hpp>

#include <cstdio>
#include <cstring>

int main() {
    using namespace threadmaxx::simd;

    // ---- 1. constexpr + scalar always true ------------------------------
    constexpr capabilities ctc = compile_time_capabilities();
    static_assert(ctc.scalar, "scalar must always be true");
    CHECK(ctc.scalar);
    std::printf("[simd_config] compile-time: scalar=%d sse2=%d avx2=%d neon=%d\n",
                int(ctc.scalar), int(ctc.sse2), int(ctc.avx2), int(ctc.neon));

    // ---- 2. runtime_capabilities — semantics post-S5 -------------------
    // Pre-S5 (S1) this was an alias for `compile_time_capabilities()`.
    // After S5 the runtime probe is independent: the runtime view
    // reflects what the host CPU actually supports, which is decoupled
    // from what the build was compiled for. The only invariant is
    // `.scalar == true`. We additionally verify the probe is stable
    // across calls (cached).
    const capabilities rtc1 = runtime_capabilities();
    const capabilities rtc2 = runtime_capabilities();
    CHECK(rtc1.scalar);
    CHECK_EQ(rtc1.scalar, rtc2.scalar);
    CHECK_EQ(rtc1.sse2,   rtc2.sse2);
    CHECK_EQ(rtc1.avx2,   rtc2.avx2);
    CHECK_EQ(rtc1.neon,   rtc2.neon);
    std::printf("[simd_config] runtime:      scalar=%d sse2=%d avx2=%d neon=%d\n",
                int(rtc1.scalar), int(rtc1.sse2),
                int(rtc1.avx2),   int(rtc1.neon));

    // ---- 3. preferred_isa consistency ----------------------------------
    constexpr isa pref = preferred_isa();
    if (pref == isa::avx2)        CHECK(ctc.avx2);
    else if (pref == isa::neon)   CHECK(ctc.neon);
    else if (pref == isa::sse2)   CHECK(ctc.sse2);
    else                          CHECK_EQ(pref, isa::scalar);
    std::printf("[simd_config] preferred=%d\n", int(pref));

    // ---- 4. enum has the four expected members (compile-time) -----------
    static_assert(static_cast<int>(isa::scalar) == 0, "scalar==0");
    static_assert(static_cast<int>(isa::sse2)   == 1, "sse2==1");
    static_assert(static_cast<int>(isa::avx2)   == 2, "avx2==2");
    static_assert(static_cast<int>(isa::neon)   == 3, "neon==3");

    // Custom-capability preferred_isa: a synthetic `scalar+sse2` set
    // should pick sse2; a `scalar+avx2+sse2` set picks avx2.
    {
        capabilities only_sse2{};
        only_sse2.sse2 = true;
        CHECK_EQ(preferred_isa_from(only_sse2), isa::sse2);

        capabilities mixed{};
        mixed.sse2 = true;
        mixed.avx2 = true;
        CHECK_EQ(preferred_isa_from(mixed), isa::avx2);

        capabilities scalar_only{};
        CHECK_EQ(preferred_isa_from(scalar_only), isa::scalar);
    }

    // ---- 5. Library version --------------------------------------------
    static_assert(THREADMAXX_SIMD_VERSION_MAJOR >= 1,
        "library is at least v1.0");
    static_assert(THREADMAXX_SIMD_VERSION >= 10000,
        "packed version matches MAJOR*10000+...");
    const char* v = version_string();
    CHECK(v != nullptr);
    CHECK(std::strlen(v) > 0);
    std::printf("[simd_config] version=%s (packed=%d)\n",
                v, THREADMAXX_SIMD_VERSION);

    EXIT_WITH_RESULT();
}
