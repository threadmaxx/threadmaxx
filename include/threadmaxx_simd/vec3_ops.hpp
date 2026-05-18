// threadmaxx_simd — public Vec3 batch kernels.
//
// Span-based, no-allocation, scalar-fallback-always. The public
// signatures here are the stable user contract; backend dispatch
// (compile-time scalar vs. AVX2 selection) happens transparently.
//
// Current dispatch (see comments on each function for rationale):
//   - `add` / `sub` / `scale` / `madd` / `dot`  →  AVX2 when built,
//     else scalar. Benchmarks show 1.1×–3.5× wins on x86_64.
//   - `normalize`  →  ALWAYS scalar (AVX2 implementation exists in
//     `detail/avx2.hpp` and is tested for correctness but is slower
//     than scalar on the workloads measured — kept as reference).
//
// Tail / mismatched-length policy:
//   - Writers (`add`/`sub`/`scale`/`madd`/`normalize`) stop at
//     `min(in.size(), out.size())` and silently no-op past. There
//     is no exception thrown for size mismatch; this matches the
//     rest of the threadmaxx public API.
//   - `dot` accumulates across `min(a.size(), b.size())` and returns
//     `0.0f` on empty input.
//   - `normalize` of the zero vector yields the zero vector (no NaN
//     from `1/sqrt(0)`).
//   - In-place aliasing is safe for the element-wise kernels:
//     calling `add(a, b, a)` or `sub(a, a, out)` produces well-
//     defined results because each step's reads finish before the
//     same step's write.

#pragma once

#include "config.hpp"
#include "detail/scalar.hpp"
#if THREADMAXX_SIMD_HAS_AVX2
#  include "detail/avx2.hpp"
#endif
#include "views.hpp"

#include <threadmaxx/Components.hpp>   // Vec3

#include <span>

namespace threadmaxx::simd {

// Compile-time dispatch: when the translation unit was built with
// `-mavx2` (or equivalent), `THREADMAXX_SIMD_HAS_AVX2 == 1` and the
// AVX2 paths take over for the element-wise kernels. The build's
// portability is unchanged: without AVX2 flags, the AVX2 header
// doesn't even open and the dispatcher resolves to scalar at
// preprocessor time.

/// out[i] = a[i] + b[i] (component-wise).
inline void add(std::span<const Vec3> a,
                std::span<const Vec3> b,
                std::span<Vec3> out) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::add(a, b, out);
#else
    detail::scalar::add(a, b, out);
#endif
}

/// out[i] = a[i] - b[i] (component-wise).
inline void sub(std::span<const Vec3> a,
                std::span<const Vec3> b,
                std::span<Vec3> out) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::sub(a, b, out);
#else
    detail::scalar::sub(a, b, out);
#endif
}

/// out[i] = in[i] * s (uniform scalar multiplier).
inline void scale(std::span<const Vec3> in,
                  float s,
                  std::span<Vec3> out) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::scale(in, s, out);
#else
    detail::scalar::scale(in, s, out);
#endif
}

/// out[i] = a[i] + b[i] * s (multiply-add).
inline void madd(std::span<const Vec3> a,
                 std::span<const Vec3> b,
                 float s,
                 std::span<Vec3> out) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::madd(a, b, s, out);
#else
    detail::scalar::madd(a, b, s, out);
#endif
}

/// out[i] = unit-length vector parallel to in[i]. Zero-magnitude
/// inputs produce zero-magnitude outputs (no NaN).
///
/// §S4 close-out: ALWAYS dispatches to the scalar path even when
/// AVX2 is built. Benchmark (`bench/simd_kernels`) shows the
/// gather-based AoS→SoA deinterleave in the AVX2 path is ~30%
/// SLOWER than scalar at 256k entities (each stride-3 gather has
/// ~10-cycle latency on Skylake; the 3-way deinterleave's 3
/// sequential gathers don't amortize their cost against the small
/// per-Vec3 compute body). The AVX2 implementation lives in
/// `detail/avx2.hpp` and stays covered by the equivalence test in
/// case a future permute-based deinterleave variant wins.
inline void normalize(std::span<const Vec3> in,
                      std::span<Vec3> out) noexcept {
    detail::scalar::normalize(in, out);
}

/// Sum of per-element dot products across the shorter span. Returns
/// `0.0f` on empty input.
inline float dot(std::span<const Vec3> a,
                 std::span<const Vec3> b) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    return detail::avx2::dot(a, b);
#else
    return detail::scalar::dot(a, b);
#endif
}

// ---- `span_view`-shaped overloads ---------------------------------------
//
// The view abstraction (§4.3 of DESIGN_NOTES.md) is the
// design-recommended call shape. Forwards to the span-shaped
// overloads above; no behavioural difference.

inline void add(span_view<const Vec3> a,
                span_view<const Vec3> b,
                span_view<Vec3> out) noexcept {
    add(a.values, b.values, out.values);
}

inline void sub(span_view<const Vec3> a,
                span_view<const Vec3> b,
                span_view<Vec3> out) noexcept {
    sub(a.values, b.values, out.values);
}

inline void scale(span_view<const Vec3> in,
                  float s,
                  span_view<Vec3> out) noexcept {
    scale(in.values, s, out.values);
}

inline void madd(span_view<const Vec3> a,
                 span_view<const Vec3> b,
                 float s,
                 span_view<Vec3> out) noexcept {
    madd(a.values, b.values, s, out.values);
}

inline void normalize(span_view<const Vec3> in,
                      span_view<Vec3> out) noexcept {
    normalize(in.values, out.values);
}

inline float dot(span_view<const Vec3> a,
                 span_view<const Vec3> b) noexcept {
    return dot(a.values, b.values);
}

} // namespace threadmaxx::simd
