// threadmaxx_simd — feature detection and dispatch enum.
//
// Compile-time only in this batch (S1). The runtime CPU probe and the
// `runtime_capabilities()` / `preferred_isa()` implementations gain
// their dynamic-dispatch teeth in S5; for now `preferred_isa()` is a
// `constexpr` shorthand for "whichever ISA the build picked up" and
// `runtime_capabilities()` matches `compile_time_capabilities()`.
//
// Macros defined here:
//
//   THREADMAXX_SIMD_HAS_SSE2  — compiler is targeting an x86 ISA with SSE2.
//   THREADMAXX_SIMD_HAS_AVX2  — compiler is targeting AVX2.
//   THREADMAXX_SIMD_HAS_NEON  — compiler is targeting ARM NEON.
//
// These never *force* a backend; they merely advertise availability so
// `*_ops.hpp` can wire compile-time dispatch in later batches. Today
// (S1) every kernel is scalar regardless.

#pragma once

#include <cstdint>

// ---- Compile-time ISA detection -----------------------------------------

#if defined(__AVX2__)
    #define THREADMAXX_SIMD_HAS_AVX2 1
#else
    #define THREADMAXX_SIMD_HAS_AVX2 0
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #define THREADMAXX_SIMD_HAS_SSE2 1
#else
    #define THREADMAXX_SIMD_HAS_SSE2 0
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define THREADMAXX_SIMD_HAS_NEON 1
#else
    #define THREADMAXX_SIMD_HAS_NEON 0
#endif

namespace threadmaxx::simd {

/// Discrete dispatch keys for the four backends the library plans to
/// ship. Today only `scalar` ever ships kernels; future batches add
/// the others.
enum class isa : std::uint8_t {
    scalar = 0,
    sse2   = 1,
    avx2   = 2,
    neon   = 3,
};

/// Per-ISA build flag snapshot. `scalar` is always true (the fallback
/// must always exist). The remaining flags reflect what the *current
/// translation unit* was built with; cross-TU consumers will see a
/// consistent answer because the macros above are stable for a given
/// compiler invocation.
struct capabilities {
    bool scalar = true;
    bool sse2   = false;
    bool avx2   = false;
    bool neon   = false;
};

/// Returns the capability set the current translation unit was built
/// with. The result is `constexpr` so the test suite can branch on it
/// at compile time and consumers can use it inside `if constexpr`.
constexpr capabilities compile_time_capabilities() noexcept {
    capabilities c{};
    c.scalar = true;
#if THREADMAXX_SIMD_HAS_SSE2
    c.sse2 = true;
#endif
#if THREADMAXX_SIMD_HAS_AVX2
    c.avx2 = true;
#endif
#if THREADMAXX_SIMD_HAS_NEON
    c.neon = true;
#endif
    return c;
}

// `runtime_capabilities()` moved to `cpu.hpp` in S5 — it now does a
// real CPUID probe rather than a compile-time alias. Users who want
// the runtime view include `<threadmaxx_simd/cpu.hpp>` directly; the
// umbrella header pulls it in too.

/// Picks the highest-performance ISA reflected by the capabilities.
/// Conservative ordering: AVX2 > NEON > SSE2 > scalar. The NEON-vs-x86
/// pick is academic since a build never sets both, but the ordering
/// keeps the function total.
constexpr isa preferred_isa_from(const capabilities& c) noexcept {
    if (c.avx2) return isa::avx2;
    if (c.neon) return isa::neon;
    if (c.sse2) return isa::sse2;
    return isa::scalar;
}

constexpr isa preferred_isa() noexcept {
    return preferred_isa_from(compile_time_capabilities());
}

} // namespace threadmaxx::simd
