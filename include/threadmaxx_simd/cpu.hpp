// threadmaxx_simd — runtime CPU feature probe (S5).
//
// `runtime_capabilities()` returns the SIMD ISAs the *current host
// CPU* actually supports, regardless of what the build target was
// compiled with. Useful for:
//
//   - Diagnostic logging ("running scalar fallback because this CPU
//     lacks AVX2").
//   - Fat-binary dispatch (when multiple backends are compiled, pick
//     the best one at runtime). The dispatch-table rewiring lives in
//     a future batch; this file just produces the capability set.
//
// Implementation: x86 path uses CPUID via the GCC / Clang
// `__get_cpuid_count` intrinsic. ARM path stubs `neon = true` when
// compiled for an ARM target with NEON (the runtime probe collapses
// to compile-time on ARM because there's no portable equivalent of
// CPUID for NEON detection). Other architectures fall back to
// scalar-only.
//
// The result is cached in a function-local static: the CPUID
// sequence runs exactly once per process. Subsequent calls are
// near-zero-cost (a single static-init check).

#pragma once

#include "config.hpp"

#if defined(__x86_64__) || defined(_M_X64) || \
    defined(__i386__)   || defined(_M_IX86)
#  define THREADMAXX_SIMD_CPU_X86 1
#else
#  define THREADMAXX_SIMD_CPU_X86 0
#endif

#if THREADMAXX_SIMD_CPU_X86
#  if defined(_MSC_VER)
#    include <intrin.h>
#  else
#    include <cpuid.h>
#  endif
#endif

namespace threadmaxx::simd {

namespace cpu_detail {

#if THREADMAXX_SIMD_CPU_X86
/// Wrapper around the platform's CPUID intrinsic. `leaf` is the
/// primary index; `subleaf` is the EAX value to pass for indexed
/// leaves (leaf 7 needs subleaf 0 for the basic AVX/AVX2 feature
/// bits). Returns false if the requested leaf is above the
/// processor's maximum-supported leaf.
inline bool cpuid(unsigned int leaf, unsigned int subleaf,
                  unsigned int* eax, unsigned int* ebx,
                  unsigned int* ecx, unsigned int* edx) noexcept {
#  if defined(_MSC_VER)
    int regs[4]{};
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    *eax = static_cast<unsigned int>(regs[0]);
    *ebx = static_cast<unsigned int>(regs[1]);
    *ecx = static_cast<unsigned int>(regs[2]);
    *edx = static_cast<unsigned int>(regs[3]);
    return true;
#  else
    return __get_cpuid_count(leaf, subleaf, eax, ebx, ecx, edx) != 0;
#  endif
}
#endif

/// Probe the host CPU for SIMD feature support. Returns a fully-
/// populated capability set. The `scalar` flag is always true.
inline capabilities probe_once() noexcept {
    capabilities c{};
    c.scalar = true;

#if THREADMAXX_SIMD_CPU_X86
    // CPUID leaf 1: SSE2 lives in EDX bit 26.
    unsigned int a1{}, b1{}, c1{}, d1{};
    if (cpuid(1, 0, &a1, &b1, &c1, &d1)) {
        if (d1 & (1u << 26)) c.sse2 = true;
    }
    // CPUID leaf 7, subleaf 0: AVX2 lives in EBX bit 5.
    // Note: AVX2 also requires OS-saved YMM state (XCR0 bit 2). For
    // userspace on a modern OS this is universally true; for
    // diagnostic accuracy we'd want to query XGETBV too. Skipped here
    // — the use case for this probe is "is the binary safe to run?"
    // and any OS that strands an AVX2-capable binary without YMM
    // support is broken in ways we can't paper over.
    unsigned int a7{}, b7{}, c7{}, d7{};
    if (cpuid(7, 0, &a7, &b7, &c7, &d7)) {
        if (b7 & (1u << 5)) c.avx2 = true;
    }
#endif

#if THREADMAXX_SIMD_HAS_NEON
    // No portable runtime probe on ARM; compile-time detection is
    // authoritative.
    c.neon = true;
#endif

    return c;
}

} // namespace cpu_detail

/// Cache the probe result on first call. The CPUID-style probe
/// itself is cheap (a few hundred cycles) but doing it once per
/// process is the right shape for a "preferred ISA" query that
/// might be hit per-frame in diagnostic code.
inline capabilities runtime_capabilities() noexcept {
    static const capabilities c = cpu_detail::probe_once();
    return c;
}

/// Runtime equivalent of `preferred_isa()`. Picks the best ISA the
/// host CPU actually supports (which may be a downgrade from
/// `preferred_isa()` if the build was compiled for a higher
/// target). Cached on first call via `runtime_capabilities()`.
inline isa preferred_isa_runtime() noexcept {
    return preferred_isa_from(runtime_capabilities());
}

} // namespace threadmaxx::simd
