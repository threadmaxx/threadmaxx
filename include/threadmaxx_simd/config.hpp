// threadmaxx_simd — compile-time feature detection and dispatch enum.
//
// This header is the foundation: it defines the `isa` enum, the
// `capabilities` POD, and the compile-time view of what SIMD ISAs
// the current translation unit was built with. Runtime probing
// (CPUID) lives in `cpu.hpp` — include that when you need the
// host CPU's actual capabilities (which can differ from compile-
// time when shipping a fat binary).
//
// Macros defined here:
//
//   THREADMAXX_SIMD_HAS_SSE2  — compiler is targeting an x86 ISA with SSE2.
//   THREADMAXX_SIMD_HAS_AVX2  — compiler is targeting AVX2.
//   THREADMAXX_SIMD_HAS_NEON  — compiler is targeting ARM NEON.
//
// These never *force* a backend; they merely advertise availability
// to the `*_ops.hpp` headers, which decide per-kernel whether the
// AVX2 / NEON / SSE2 paths are dispatched (benchmark-driven choice).

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
/// `scalar` is true via the struct's default initializer.
constexpr capabilities compile_time_capabilities() noexcept {
    capabilities c{};
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
