// threadmaxx_simd — runtime CPU probe regression test (S5).
//
// Verifies:
//   1. `runtime_capabilities()` is callable and returns
//      `.scalar == true`.
//   2. Repeated calls produce identical results (probe is cached).
//   3. `preferred_isa_runtime()` returns a value consistent with
//      the runtime capability set (never claims an ISA the host
//      doesn't support).
//   4. On x86_64 hosts with AVX2 (the dev target), at least
//      `.sse2 == true` (any AVX2-capable CPU also implements SSE2).
//      A weaker invariant that catches a broken probe.
//   5. On hosts that DO support AVX2 at runtime, `preferred_isa_runtime()`
//      reports `isa::avx2` (consistency).

#include "Check.hpp"

#include <threadmaxx_simd/config.hpp>
#include <threadmaxx_simd/cpu.hpp>

#include <cstdio>

int main() {
    using namespace threadmaxx::simd;

    // ---- 1. Basic callability + scalar invariant -----------------------
    const capabilities rtc = runtime_capabilities();
    CHECK(rtc.scalar);
    std::printf("[simd_runtime_dispatch] host: scalar=%d sse2=%d avx2=%d neon=%d\n",
                int(rtc.scalar), int(rtc.sse2),
                int(rtc.avx2),   int(rtc.neon));

    // ---- 2. Cache stability across calls -------------------------------
    for (int i = 0; i < 5; ++i) {
        const capabilities c = runtime_capabilities();
        CHECK_EQ(c.scalar, rtc.scalar);
        CHECK_EQ(c.sse2,   rtc.sse2);
        CHECK_EQ(c.avx2,   rtc.avx2);
        CHECK_EQ(c.neon,   rtc.neon);
    }
    std::printf("[simd_runtime_dispatch] cache stable across calls\n");

    // ---- 3. preferred_isa_runtime consistency --------------------------
    const isa rt = preferred_isa_runtime();
    if (rt == isa::avx2)        CHECK(rtc.avx2);
    else if (rt == isa::neon)   CHECK(rtc.neon);
    else if (rt == isa::sse2)   CHECK(rtc.sse2);
    else                        CHECK_EQ(rt, isa::scalar);
    std::printf("[simd_runtime_dispatch] preferred_isa_runtime=%d\n", int(rt));

    // ---- 4. AVX2 implies SSE2 ------------------------------------------
    // x86 ISA ordering: any CPU with AVX2 must also have SSE2.
    if (rtc.avx2) CHECK(rtc.sse2);

    // ---- 5. When AVX2 is available, it's the preferred runtime ISA ----
    if (rtc.avx2) {
        CHECK_EQ(preferred_isa_runtime(), isa::avx2);
    }

    EXIT_WITH_RESULT();
}
