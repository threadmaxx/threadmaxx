// threadmaxx_simd — AVX2 backend for Vec3 kernels.
//
// Vectorizes the element-wise kernels (`add` / `sub` / `scale` /
// `madd` / `dot`) by treating a `std::span<Vec3>` as a flat float
// array of length `3 * n`. AVX2 processes 8 floats per lane, so we
// step in chunks of 8 floats (= 2⅔ Vec3s, but float-wise that
// doesn't matter — element-wise ops don't care about Vec3
// boundaries). A scalar tail handles the last 0..7 floats.
//
// `normalize` is NOT in this file: it's the only Vec3 kernel that's
// inherently per-Vec3 (it needs `len² = x² + y² + z²` per element,
// which requires a 3-way AoS↔SoA deinterleave under AVX2). Logged
// as a follow-up — see FUTURE_WORK S3.5.
//
// Aliasing note: we cast `Vec3*` → `float*` to access the contiguous
// flat layout. `Vec3` is standard-layout with first member `float x`,
// so `&v == &v.x` is pointer-interconvertible. Treating the array of
// Vec3s as `float[3*n]` is the documented common pattern in ECS /
// game-engine code; compilers (GCC / Clang / MSVC) treat it as
// non-aliasing under their effective-type rules. The cast lives
// only in `detail::` so user code never sees it (per DESIGN_NOTES
// §9 non-goals: "No public reliance on `reinterpret_cast`").

#pragma once

#include "../config.hpp"

#if THREADMAXX_SIMD_HAS_AVX2

#include "../simd_math.hpp"
#include "scalar.hpp"   // tail-path falls back to scalar helpers

#include <threadmaxx/Components.hpp>           // Vec3, Quat, Transform, BoundingVolume
#include <threadmaxx/render/Visibility.hpp>    // Frustum

#include <immintrin.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace threadmaxx::simd::detail::avx2 {

/// Horizontal sum of an AVX2 register's 8 floats into a single
/// scalar. Two halves → 128-bit add → two `hadd` → low-element
/// extract. Standard pattern; good enough for the dot-product
/// epilogue.
inline float horizontal_sum_ps(__m256 v) noexcept {
    const __m128 lo = _mm256_castps256_ps128(v);
    const __m128 hi = _mm256_extractf128_ps(v, 1);
    __m128 s = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s);
    s = _mm_hadd_ps(s, s);
    return _mm_cvtss_f32(s);
}

inline void add(std::span<const Vec3> a,
                std::span<const Vec3> b,
                std::span<Vec3> out) noexcept {
    const std::size_t n = std::min({a.size(), b.size(), out.size()});
    const std::size_t total = n * 3;
    const float* af = reinterpret_cast<const float*>(a.data());
    const float* bf = reinterpret_cast<const float*>(b.data());
    float*       cf = reinterpret_cast<float*>      (out.data());

    std::size_t i = 0;
    for (; i + 8 <= total; i += 8) {
        const __m256 va = _mm256_loadu_ps(af + i);
        const __m256 vb = _mm256_loadu_ps(bf + i);
        _mm256_storeu_ps(cf + i, _mm256_add_ps(va, vb));
    }
    for (; i < total; ++i) cf[i] = af[i] + bf[i];
}

inline void sub(std::span<const Vec3> a,
                std::span<const Vec3> b,
                std::span<Vec3> out) noexcept {
    const std::size_t n = std::min({a.size(), b.size(), out.size()});
    const std::size_t total = n * 3;
    const float* af = reinterpret_cast<const float*>(a.data());
    const float* bf = reinterpret_cast<const float*>(b.data());
    float*       cf = reinterpret_cast<float*>      (out.data());

    std::size_t i = 0;
    for (; i + 8 <= total; i += 8) {
        const __m256 va = _mm256_loadu_ps(af + i);
        const __m256 vb = _mm256_loadu_ps(bf + i);
        _mm256_storeu_ps(cf + i, _mm256_sub_ps(va, vb));
    }
    for (; i < total; ++i) cf[i] = af[i] - bf[i];
}

inline void scale(std::span<const Vec3> in,
                  float s,
                  std::span<Vec3> out) noexcept {
    const std::size_t n = std::min(in.size(), out.size());
    const std::size_t total = n * 3;
    const float* af = reinterpret_cast<const float*>(in.data());
    float*       cf = reinterpret_cast<float*>      (out.data());
    const __m256 vs = _mm256_set1_ps(s);

    std::size_t i = 0;
    for (; i + 8 <= total; i += 8) {
        const __m256 va = _mm256_loadu_ps(af + i);
        _mm256_storeu_ps(cf + i, _mm256_mul_ps(va, vs));
    }
    for (; i < total; ++i) cf[i] = af[i] * s;
}

inline void madd(std::span<const Vec3> a,
                 std::span<const Vec3> b,
                 float s,
                 std::span<Vec3> out) noexcept {
    const std::size_t n = std::min({a.size(), b.size(), out.size()});
    const std::size_t total = n * 3;
    const float* af = reinterpret_cast<const float*>(a.data());
    const float* bf = reinterpret_cast<const float*>(b.data());
    float*       cf = reinterpret_cast<float*>      (out.data());
    const __m256 vs = _mm256_set1_ps(s);

    std::size_t i = 0;
    for (; i + 8 <= total; i += 8) {
        const __m256 va = _mm256_loadu_ps(af + i);
        const __m256 vb = _mm256_loadu_ps(bf + i);
#if defined(__FMA__)
        _mm256_storeu_ps(cf + i, _mm256_fmadd_ps(vb, vs, va));
#else
        _mm256_storeu_ps(cf + i,
            _mm256_add_ps(va, _mm256_mul_ps(vb, vs)));
#endif
    }
#if defined(__FMA__)
    for (; i < total; ++i) cf[i] = std::fma(bf[i], s, af[i]);
#else
    for (; i < total; ++i) cf[i] = af[i] + bf[i] * s;
#endif
}

/// Per-Vec3 normalize. Unlike the element-wise kernels above, this
/// can't operate on a flat-float stream: the `len² = x² + y² + z²`
/// reduction crosses 3 consecutive floats, so we need an AoS↔SoA
/// step. The chosen approach (S3.5):
///
///   1. Gather 8 contiguous Vec3s' x / y / z components into three
///      AVX2 registers via `_mm256_i32gather_ps` with pre-built
///      strided indices. Gather is decently fast on Skylake+.
///   2. Compute `len² = xs*xs + ys*ys + zs*zs`, then
///      `invLen = 1 / sqrt(len²)` via `sqrt_ps + div_ps`. Full
///      float precision — within 1 ulp of the scalar reference.
///   3. Mask zero-magnitude entries: `len² <= 0 → invLen = 0`. The
///      subsequent multiplications then produce the zero output the
///      design contract demands (no NaN propagation through
///      `1/sqrt(0)`).
///   4. Reinterleave by extracting 8 lanes from each output
///      register and writing per-Vec3. Simple scalar epilogue; a
///      SIMD reinterleave is a future micro-optimization (it would
///      shave ~5 cycles per batch).
///
/// Tail (n % 8) is handled by a scalar epilogue.
inline void normalize(std::span<const Vec3> in,
                      std::span<Vec3> out) noexcept {
    const std::size_t n = std::min(in.size(), out.size());
    const float* inf  = reinterpret_cast<const float*>(in.data());
    float*       outf = reinterpret_cast<float*>      (out.data());

    const __m256i idxX = _mm256_setr_epi32( 0,  3,  6,  9, 12, 15, 18, 21);
    const __m256i idxY = _mm256_setr_epi32( 1,  4,  7, 10, 13, 16, 19, 22);
    const __m256i idxZ = _mm256_setr_epi32( 2,  5,  8, 11, 14, 17, 20, 23);
    const __m256 one   = _mm256_set1_ps(1.0f);
    const __m256 zero  = _mm256_setzero_ps();

    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        const float* base = inf + i * 3;
        const __m256 xs = _mm256_i32gather_ps(base, idxX, 4);
        const __m256 ys = _mm256_i32gather_ps(base, idxY, 4);
        const __m256 zs = _mm256_i32gather_ps(base, idxZ, 4);

        const __m256 len2 = _mm256_add_ps(
            _mm256_add_ps(_mm256_mul_ps(xs, xs), _mm256_mul_ps(ys, ys)),
            _mm256_mul_ps(zs, zs));
        const __m256 zeroMask = _mm256_cmp_ps(len2, zero, _CMP_LE_OQ);
        const __m256 invLenRaw = _mm256_div_ps(one, _mm256_sqrt_ps(len2));
        // andnot(zeroMask, invLenRaw): keep invLenRaw where mask bit
        // is 0 (len² > 0), zero out where mask bit is 1 (len² <= 0).
        const __m256 invLen = _mm256_andnot_ps(zeroMask, invLenRaw);

        alignas(32) float xa[8], ya[8], za[8];
        _mm256_store_ps(xa, _mm256_mul_ps(xs, invLen));
        _mm256_store_ps(ya, _mm256_mul_ps(ys, invLen));
        _mm256_store_ps(za, _mm256_mul_ps(zs, invLen));
        for (std::size_t j = 0; j < 8; ++j) {
            outf[(i + j) * 3 + 0] = xa[j];
            outf[(i + j) * 3 + 1] = ya[j];
            outf[(i + j) * 3 + 2] = za[j];
        }
    }

    for (; i < n; ++i) {
        const float x = inf[i * 3 + 0];
        const float y = inf[i * 3 + 1];
        const float z = inf[i * 3 + 2];
        const float len2 = x * x + y * y + z * z;
        if (len2 <= 0.0f) {
            outf[i * 3 + 0] = 0.0f;
            outf[i * 3 + 1] = 0.0f;
            outf[i * 3 + 2] = 0.0f;
        } else {
            const float inv = 1.0f / std::sqrt(len2);
            outf[i * 3 + 0] = x * inv;
            outf[i * 3 + 1] = y * inv;
            outf[i * 3 + 2] = z * inv;
        }
    }
}

/// Per-pair dot product summed across the spans. Treating the flat
/// stream as `[ax, ay, az, bx, by, bz, ...]`, the sum of pairwise
/// products `Σ af[i] * bf[i]` equals the sum of per-Vec3 dot
/// products — algebraically identical, no rearrangement needed.
inline float dot(std::span<const Vec3> a,
                 std::span<const Vec3> b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    const std::size_t total = n * 3;
    const float* af = reinterpret_cast<const float*>(a.data());
    const float* bf = reinterpret_cast<const float*>(b.data());

    __m256 acc = _mm256_setzero_ps();
    std::size_t i = 0;
    for (; i + 8 <= total; i += 8) {
        const __m256 va = _mm256_loadu_ps(af + i);
        const __m256 vb = _mm256_loadu_ps(bf + i);
#if defined(__FMA__)
        acc = _mm256_fmadd_ps(va, vb, acc);
#else
        acc = _mm256_add_ps(acc, _mm256_mul_ps(va, vb));
#endif
    }
    float result = horizontal_sum_ps(acc);
    for (; i < total; ++i) result += af[i] * bf[i];
    return result;
}

// ---- Quat kernels --------------------------------------------------------

/// In-place per-element normalize. A `Quat` is 16 bytes / 4 floats —
/// half an AVX2 register. We pack two quats per iteration (8 floats
/// per __m256). The `_mm256_dp_ps(v, v, 0xFF)` op computes the dot
/// product across all 4 floats of each 128-bit lane and broadcasts
/// the result across that lane's 4 positions — exactly the
/// length-squared we need for each of the two quats. Zero-norm
/// quats become identity `(0,0,0,1)` via a blend with a fixed
/// identity vector.
inline void quat_normalize(std::span<Quat> q) noexcept {
    const std::size_t n = q.size();
    float* qf = reinterpret_cast<float*>(q.data());

    const __m256 one      = _mm256_set1_ps(1.0f);
    const __m256 zero     = _mm256_setzero_ps();
    // Identity-quaternion vector for the two-quat lane layout:
    // [0,0,0,1, 0,0,0,1].
    const __m256 identity = _mm256_setr_ps(0, 0, 0, 1, 0, 0, 0, 1);

    std::size_t i = 0;
    for (; i + 2 <= n; i += 2) {
        const __m256 v = _mm256_loadu_ps(qf + i * 4);
        // dp_ps with mask 0xFF: multiply all 4 lanes, sum, broadcast
        // back. Operates per 128-bit lane, so we get len²(q0)
        // broadcast in low 128 and len²(q1) broadcast in high 128.
        const __m256 len2     = _mm256_dp_ps(v, v, 0xFF);
        const __m256 zeroMask = _mm256_cmp_ps(len2, zero, _CMP_LE_OQ);
        const __m256 invLen   = _mm256_div_ps(one, _mm256_sqrt_ps(len2));
        const __m256 scaled   = _mm256_mul_ps(v, invLen);
        // Where the lane's mask is set (zero-norm), pick identity
        // instead of the (NaN-or-zero) scaled result.
        const __m256 result   = _mm256_blendv_ps(scaled, identity, zeroMask);
        _mm256_storeu_ps(qf + i * 4, result);
    }
    // Tail: 0 or 1 quat left.
    for (; i < n; ++i) {
        const float x = qf[i * 4 + 0];
        const float y = qf[i * 4 + 1];
        const float z = qf[i * 4 + 2];
        const float w = qf[i * 4 + 3];
        const float len2 = x * x + y * y + z * z + w * w;
        if (len2 <= 0.0f) {
            qf[i * 4 + 0] = 0.0f;
            qf[i * 4 + 1] = 0.0f;
            qf[i * 4 + 2] = 0.0f;
            qf[i * 4 + 3] = 1.0f;
        } else {
            const float inv = 1.0f / std::sqrt(len2);
            qf[i * 4 + 0] = x * inv;
            qf[i * 4 + 1] = y * inv;
            qf[i * 4 + 2] = z * inv;
            qf[i * 4 + 3] = w * inv;
        }
    }
}

// ---- Frustum-cull kernel -------------------------------------------------

/// Vectorize across 8 spheres at a time. Per iteration:
///   1. Gather 8 lanes of center.x / .y / .z via stride-3 indices
///      (same pattern as Vec3 normalize).
///   2. Load 8 contiguous radii.
///   3. For each of the 6 planes: dist = p.x*xs + p.y*ys + p.z*zs +
///      p.w, broadcast scalars. Lane is "outside" iff `dist < -r`.
///      Accumulate a 256-bit "still visible" mask via AND.
///   4. Reduce the 8-wide visibility mask to a packed 8-bit value
///      via `_mm256_movemask_ps`, then expand bit-by-bit into the
///      output `uint8_t` span. Cheap (8 scalar writes per batch).
inline void frustum_cull(std::span<const Vec3> centers,
                         std::span<const float> radii,
                         const Frustum& frustum,
                         std::span<std::uint8_t> visible_mask) noexcept {
    const std::size_t n = std::min({centers.size(), radii.size(),
                                    visible_mask.size()});
    const float* cf = reinterpret_cast<const float*>(centers.data());
    const float* rf = radii.data();

    const __m256i idxX = _mm256_setr_epi32( 0,  3,  6,  9, 12, 15, 18, 21);
    const __m256i idxY = _mm256_setr_epi32( 1,  4,  7, 10, 13, 16, 19, 22);
    const __m256i idxZ = _mm256_setr_epi32( 2,  5,  8, 11, 14, 17, 20, 23);
    const __m256 allBitsSet = _mm256_castsi256_ps(_mm256_set1_epi32(-1));

    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        const float* base = cf + i * 3;
        const __m256 xs = _mm256_i32gather_ps(base, idxX, 4);
        const __m256 ys = _mm256_i32gather_ps(base, idxY, 4);
        const __m256 zs = _mm256_i32gather_ps(base, idxZ, 4);
        const __m256 rs = _mm256_loadu_ps(rf + i);
        const __m256 negR = _mm256_sub_ps(_mm256_setzero_ps(), rs);

        // Start "still visible" = all-1s; AND in per-plane survival.
        __m256 vis = allBitsSet;
        for (const auto& p : frustum.planes) {
            const __m256 px = _mm256_set1_ps(p[0]);
            const __m256 py = _mm256_set1_ps(p[1]);
            const __m256 pz = _mm256_set1_ps(p[2]);
            const __m256 pw = _mm256_set1_ps(p[3]);
            // dist = px*xs + py*ys + pz*zs + pw
#if defined(__FMA__)
            __m256 dist = _mm256_fmadd_ps(px, xs, pw);
            dist        = _mm256_fmadd_ps(py, ys, dist);
            dist        = _mm256_fmadd_ps(pz, zs, dist);
#else
            __m256 dist = _mm256_add_ps(pw, _mm256_mul_ps(px, xs));
            dist        = _mm256_add_ps(dist, _mm256_mul_ps(py, ys));
            dist        = _mm256_add_ps(dist, _mm256_mul_ps(pz, zs));
#endif
            // survive = (dist >= -r)  ↔  NOT (dist < -r)
            // _CMP_GE_OQ returns all-bits-set when true (visible).
            const __m256 survive = _mm256_cmp_ps(dist, negR, _CMP_GE_OQ);
            vis = _mm256_and_ps(vis, survive);
        }
        // Pack the 8 lanes to 8 bits, then expand to 8 bytes.
        const int packed = _mm256_movemask_ps(vis);
        for (int j = 0; j < 8; ++j) {
            visible_mask[i + j] = static_cast<std::uint8_t>(
                (packed >> j) & 1);
        }
    }
    // Scalar tail.
    for (; i < n; ++i) {
        std::uint8_t vis = 1;
        for (const auto& p : frustum.planes) {
            const float d = p[0] * centers[i].x + p[1] * centers[i].y +
                            p[2] * centers[i].z + p[3];
            if (d < -radii[i]) { vis = 0; break; }
        }
        visible_mask[i] = vis;
    }
}

// ---- Horizontal-reduction helpers ----------------------------------------

inline float hmin_ps(__m256 v) noexcept {
    const __m128 lo = _mm256_castps256_ps128(v);
    const __m128 hi = _mm256_extractf128_ps(v, 1);
    __m128 m = _mm_min_ps(lo, hi);
    m = _mm_min_ps(m, _mm_movehl_ps(m, m));
    m = _mm_min_ss(m, _mm_shuffle_ps(m, m, 0x55));
    return _mm_cvtss_f32(m);
}

inline float hmax_ps(__m256 v) noexcept {
    const __m128 lo = _mm256_castps256_ps128(v);
    const __m128 hi = _mm256_extractf128_ps(v, 1);
    __m128 m = _mm_max_ps(lo, hi);
    m = _mm_max_ps(m, _mm_movehl_ps(m, m));
    m = _mm_max_ss(m, _mm_shuffle_ps(m, m, 0x55));
    return _mm_cvtss_f32(m);
}

// ---- Transform kernels ---------------------------------------------------

/// Apply per-pair (Transform, Vec3 point) → Vec3 output. Vectorizes
/// across 8 pairs at a time via 10-stride gathers for Transform
/// fields (40-byte Transform = 10 floats; pos/quat/scale all at
/// known offsets) and 3-stride gathers for Vec3 points. The
/// quaternion rotation uses the Rodrigues identity (same scalar
/// formula as `quat_rotate_vec_one`) lifted to 8-wide. Scalar
/// reinterleave writes the 8 output Vec3s back to AoS.
inline void apply_transforms(std::span<const Transform> t,
                             std::span<const Vec3> points,
                             std::span<Vec3> out) noexcept {
    const std::size_t n = std::min({t.size(), points.size(), out.size()});
    const float* tf = reinterpret_cast<const float*>(t.data());
    const float* pf = reinterpret_cast<const float*>(points.data());
    float*       of = reinterpret_cast<float*>      (out.data());

    // 10-stride indices for Transform's 10 float members.
    const __m256i tIdxPx = _mm256_setr_epi32(0, 10, 20, 30, 40, 50, 60, 70);
    const __m256i tIdxPy = _mm256_setr_epi32(1, 11, 21, 31, 41, 51, 61, 71);
    const __m256i tIdxPz = _mm256_setr_epi32(2, 12, 22, 32, 42, 52, 62, 72);
    const __m256i tIdxQx = _mm256_setr_epi32(3, 13, 23, 33, 43, 53, 63, 73);
    const __m256i tIdxQy = _mm256_setr_epi32(4, 14, 24, 34, 44, 54, 64, 74);
    const __m256i tIdxQz = _mm256_setr_epi32(5, 15, 25, 35, 45, 55, 65, 75);
    const __m256i tIdxQw = _mm256_setr_epi32(6, 16, 26, 36, 46, 56, 66, 76);
    const __m256i tIdxSx = _mm256_setr_epi32(7, 17, 27, 37, 47, 57, 67, 77);
    const __m256i tIdxSy = _mm256_setr_epi32(8, 18, 28, 38, 48, 58, 68, 78);
    const __m256i tIdxSz = _mm256_setr_epi32(9, 19, 29, 39, 49, 59, 69, 79);
    // 3-stride indices for Vec3 points.
    const __m256i pIdxX = _mm256_setr_epi32(0, 3, 6, 9, 12, 15, 18, 21);
    const __m256i pIdxY = _mm256_setr_epi32(1, 4, 7, 10, 13, 16, 19, 22);
    const __m256i pIdxZ = _mm256_setr_epi32(2, 5, 8, 11, 14, 17, 20, 23);

    const __m256 two = _mm256_set1_ps(2.0f);

    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        const float* tBase = tf + i * 10;
        const float* pBase = pf + i * 3;

        const __m256 px = _mm256_i32gather_ps(tBase, tIdxPx, 4);
        const __m256 py = _mm256_i32gather_ps(tBase, tIdxPy, 4);
        const __m256 pz = _mm256_i32gather_ps(tBase, tIdxPz, 4);
        const __m256 qx = _mm256_i32gather_ps(tBase, tIdxQx, 4);
        const __m256 qy = _mm256_i32gather_ps(tBase, tIdxQy, 4);
        const __m256 qz = _mm256_i32gather_ps(tBase, tIdxQz, 4);
        const __m256 qw = _mm256_i32gather_ps(tBase, tIdxQw, 4);
        const __m256 sx = _mm256_i32gather_ps(tBase, tIdxSx, 4);
        const __m256 sy = _mm256_i32gather_ps(tBase, tIdxSy, 4);
        const __m256 sz = _mm256_i32gather_ps(tBase, tIdxSz, 4);

        const __m256 vx = _mm256_i32gather_ps(pBase, pIdxX, 4);
        const __m256 vy = _mm256_i32gather_ps(pBase, pIdxY, 4);
        const __m256 vz = _mm256_i32gather_ps(pBase, pIdxZ, 4);

        // Apply per-component scale.
        const __m256 svx = _mm256_mul_ps(vx, sx);
        const __m256 svy = _mm256_mul_ps(vy, sy);
        const __m256 svz = _mm256_mul_ps(vz, sz);

        // Rodrigues: u = q.xyz × sv + q.w * sv
#if defined(__FMA__)
        const __m256 ux = _mm256_fmadd_ps(qw, svx,
            _mm256_sub_ps(_mm256_mul_ps(qy, svz), _mm256_mul_ps(qz, svy)));
        const __m256 uy = _mm256_fmadd_ps(qw, svy,
            _mm256_sub_ps(_mm256_mul_ps(qz, svx), _mm256_mul_ps(qx, svz)));
        const __m256 uz = _mm256_fmadd_ps(qw, svz,
            _mm256_sub_ps(_mm256_mul_ps(qx, svy), _mm256_mul_ps(qy, svx)));
#else
        const __m256 ux = _mm256_add_ps(_mm256_mul_ps(qw, svx),
            _mm256_sub_ps(_mm256_mul_ps(qy, svz), _mm256_mul_ps(qz, svy)));
        const __m256 uy = _mm256_add_ps(_mm256_mul_ps(qw, svy),
            _mm256_sub_ps(_mm256_mul_ps(qz, svx), _mm256_mul_ps(qx, svz)));
        const __m256 uz = _mm256_add_ps(_mm256_mul_ps(qw, svz),
            _mm256_sub_ps(_mm256_mul_ps(qx, svy), _mm256_mul_ps(qy, svx)));
#endif
        // out_xyz = sv + 2 * (q.xyz × u)  +  position
        const __m256 ccx = _mm256_sub_ps(_mm256_mul_ps(qy, uz),
                                         _mm256_mul_ps(qz, uy));
        const __m256 ccy = _mm256_sub_ps(_mm256_mul_ps(qz, ux),
                                         _mm256_mul_ps(qx, uz));
        const __m256 ccz = _mm256_sub_ps(_mm256_mul_ps(qx, uy),
                                         _mm256_mul_ps(qy, ux));
#if defined(__FMA__)
        __m256 ox = _mm256_fmadd_ps(two, ccx, svx);
        __m256 oy = _mm256_fmadd_ps(two, ccy, svy);
        __m256 oz = _mm256_fmadd_ps(two, ccz, svz);
#else
        __m256 ox = _mm256_add_ps(svx, _mm256_mul_ps(two, ccx));
        __m256 oy = _mm256_add_ps(svy, _mm256_mul_ps(two, ccy));
        __m256 oz = _mm256_add_ps(svz, _mm256_mul_ps(two, ccz));
#endif
        ox = _mm256_add_ps(ox, px);
        oy = _mm256_add_ps(oy, py);
        oz = _mm256_add_ps(oz, pz);

        // Reinterleave + store.
        alignas(32) float xa[8], ya[8], za[8];
        _mm256_store_ps(xa, ox);
        _mm256_store_ps(ya, oy);
        _mm256_store_ps(za, oz);
        for (std::size_t j = 0; j < 8; ++j) {
            of[(i + j) * 3 + 0] = xa[j];
            of[(i + j) * 3 + 1] = ya[j];
            of[(i + j) * 3 + 2] = za[j];
        }
    }

    // Scalar tail — delegate to the scalar reference impl on a
    // sub-slice. Cheaper than open-coding the rotation again.
    if (i < n) {
        scalar::apply_transforms(t.subspan(i), points.subspan(i),
                                 out.subspan(i));
    }
}

/// In-place per-pair `position += velocity * dt`. Transform is
/// 10-stride; we gather position fields, the velocity span is
/// 3-stride. Output is a scalar scatter (no AVX2 fp scatter).
inline void integrate_linear_motion(std::span<Transform> t,
                                    std::span<const Vec3> velocity,
                                    float dt) noexcept {
    const std::size_t n = std::min(t.size(), velocity.size());
    float* tf = reinterpret_cast<float*>(t.data());
    const float* vf = reinterpret_cast<const float*>(velocity.data());

    const __m256i tIdxPx = _mm256_setr_epi32(0, 10, 20, 30, 40, 50, 60, 70);
    const __m256i tIdxPy = _mm256_setr_epi32(1, 11, 21, 31, 41, 51, 61, 71);
    const __m256i tIdxPz = _mm256_setr_epi32(2, 12, 22, 32, 42, 52, 62, 72);
    const __m256i vIdxX  = _mm256_setr_epi32(0, 3, 6, 9, 12, 15, 18, 21);
    const __m256i vIdxY  = _mm256_setr_epi32(1, 4, 7, 10, 13, 16, 19, 22);
    const __m256i vIdxZ  = _mm256_setr_epi32(2, 5, 8, 11, 14, 17, 20, 23);
    const __m256 dt8 = _mm256_set1_ps(dt);

    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        float* tBase = tf + i * 10;
        const float* vBase = vf + i * 3;

        const __m256 px = _mm256_i32gather_ps(tBase, tIdxPx, 4);
        const __m256 py = _mm256_i32gather_ps(tBase, tIdxPy, 4);
        const __m256 pz = _mm256_i32gather_ps(tBase, tIdxPz, 4);
        const __m256 vx = _mm256_i32gather_ps(vBase, vIdxX, 4);
        const __m256 vy = _mm256_i32gather_ps(vBase, vIdxY, 4);
        const __m256 vz = _mm256_i32gather_ps(vBase, vIdxZ, 4);

#if defined(__FMA__)
        const __m256 npx = _mm256_fmadd_ps(vx, dt8, px);
        const __m256 npy = _mm256_fmadd_ps(vy, dt8, py);
        const __m256 npz = _mm256_fmadd_ps(vz, dt8, pz);
#else
        const __m256 npx = _mm256_add_ps(px, _mm256_mul_ps(vx, dt8));
        const __m256 npy = _mm256_add_ps(py, _mm256_mul_ps(vy, dt8));
        const __m256 npz = _mm256_add_ps(pz, _mm256_mul_ps(vz, dt8));
#endif

        alignas(32) float xa[8], ya[8], za[8];
        _mm256_store_ps(xa, npx);
        _mm256_store_ps(ya, npy);
        _mm256_store_ps(za, npz);
        for (std::size_t j = 0; j < 8; ++j) {
            tBase[j * 10 + 0] = xa[j];
            tBase[j * 10 + 1] = ya[j];
            tBase[j * 10 + 2] = za[j];
        }
    }

    if (i < n) {
        scalar::integrate_linear_motion(t.subspan(i), velocity.subspan(i), dt);
    }
}

// ---- AABB kernel ---------------------------------------------------------

/// Per-AABB transform: build the 8 corners, broadcast the matching
/// Transform across 8 lanes, apply scale + quat rotation + position
/// translation, then 8-wide horizontal min/max to recover the
/// axis-aligned bound. One AABB per iteration; the parallelism is
/// across corners (8 corners per AABB).
inline void transform_aabb(std::span<const Transform> t,
                           std::span<const BoundingVolume> in,
                           std::span<BoundingVolume> out) noexcept {
    const std::size_t n = std::min({t.size(), in.size(), out.size()});
    const __m256 two = _mm256_set1_ps(2.0f);

    for (std::size_t i = 0; i < n; ++i) {
        const float mnx = in[i].min.x, mny = in[i].min.y, mnz = in[i].min.z;
        const float mxx = in[i].max.x, mxy = in[i].max.y, mxz = in[i].max.z;

        // 8 corners as (cx, cy, cz) component-wise. The 8 combinations
        // of (mn|mx) for each axis.
        const __m256 cx = _mm256_setr_ps(mnx, mxx, mnx, mxx, mnx, mxx, mnx, mxx);
        const __m256 cy = _mm256_setr_ps(mny, mny, mxy, mxy, mny, mny, mxy, mxy);
        const __m256 cz = _mm256_setr_ps(mnz, mnz, mnz, mnz, mxz, mxz, mxz, mxz);

        const __m256 sx = _mm256_set1_ps(t[i].scale.x);
        const __m256 sy = _mm256_set1_ps(t[i].scale.y);
        const __m256 sz = _mm256_set1_ps(t[i].scale.z);
        const __m256 qx = _mm256_set1_ps(t[i].orientation.x);
        const __m256 qy = _mm256_set1_ps(t[i].orientation.y);
        const __m256 qz = _mm256_set1_ps(t[i].orientation.z);
        const __m256 qw = _mm256_set1_ps(t[i].orientation.w);
        const __m256 px = _mm256_set1_ps(t[i].position.x);
        const __m256 py = _mm256_set1_ps(t[i].position.y);
        const __m256 pz = _mm256_set1_ps(t[i].position.z);

        const __m256 svx = _mm256_mul_ps(cx, sx);
        const __m256 svy = _mm256_mul_ps(cy, sy);
        const __m256 svz = _mm256_mul_ps(cz, sz);

#if defined(__FMA__)
        const __m256 ux = _mm256_fmadd_ps(qw, svx,
            _mm256_sub_ps(_mm256_mul_ps(qy, svz), _mm256_mul_ps(qz, svy)));
        const __m256 uy = _mm256_fmadd_ps(qw, svy,
            _mm256_sub_ps(_mm256_mul_ps(qz, svx), _mm256_mul_ps(qx, svz)));
        const __m256 uz = _mm256_fmadd_ps(qw, svz,
            _mm256_sub_ps(_mm256_mul_ps(qx, svy), _mm256_mul_ps(qy, svx)));
#else
        const __m256 ux = _mm256_add_ps(_mm256_mul_ps(qw, svx),
            _mm256_sub_ps(_mm256_mul_ps(qy, svz), _mm256_mul_ps(qz, svy)));
        const __m256 uy = _mm256_add_ps(_mm256_mul_ps(qw, svy),
            _mm256_sub_ps(_mm256_mul_ps(qz, svx), _mm256_mul_ps(qx, svz)));
        const __m256 uz = _mm256_add_ps(_mm256_mul_ps(qw, svz),
            _mm256_sub_ps(_mm256_mul_ps(qx, svy), _mm256_mul_ps(qy, svx)));
#endif
        const __m256 ccx = _mm256_sub_ps(_mm256_mul_ps(qy, uz),
                                         _mm256_mul_ps(qz, uy));
        const __m256 ccy = _mm256_sub_ps(_mm256_mul_ps(qz, ux),
                                         _mm256_mul_ps(qx, uz));
        const __m256 ccz = _mm256_sub_ps(_mm256_mul_ps(qx, uy),
                                         _mm256_mul_ps(qy, ux));
#if defined(__FMA__)
        __m256 ox = _mm256_fmadd_ps(two, ccx, svx);
        __m256 oy = _mm256_fmadd_ps(two, ccy, svy);
        __m256 oz = _mm256_fmadd_ps(two, ccz, svz);
#else
        __m256 ox = _mm256_add_ps(svx, _mm256_mul_ps(two, ccx));
        __m256 oy = _mm256_add_ps(svy, _mm256_mul_ps(two, ccy));
        __m256 oz = _mm256_add_ps(svz, _mm256_mul_ps(two, ccz));
#endif
        ox = _mm256_add_ps(ox, px);
        oy = _mm256_add_ps(oy, py);
        oz = _mm256_add_ps(oz, pz);

        out[i].min = Vec3{hmin_ps(ox), hmin_ps(oy), hmin_ps(oz)};
        out[i].max = Vec3{hmax_ps(ox), hmax_ps(oy), hmax_ps(oz)};
    }
}

} // namespace threadmaxx::simd::detail::avx2

#endif // THREADMAXX_SIMD_HAS_AVX2
