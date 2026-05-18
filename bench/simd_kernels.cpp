// threadmaxx_simd — scalar vs AVX2 micro-benchmark (§S4 close-out).
//
// Runs each vectorized kernel against a deterministic RNG-seeded
// dataset at three entity counts (1k / 16k / 256k) and reports
// per-iteration mean / p50 / p95 / p99 timings plus the
// scalar-vs-AVX2 speedup ratio. Output is CSV-compatible (the
// shared `BenchRow` framework from §3.9.1 batch 16).
//
// Build: `-DTHREADMAXX_BUILD_BENCHMARKS=ON` + `-DTHREADMAXX_BUILD_SIMD=ON`
// (both ON by default).
//
// Invocation: `./bench/simd_kernels [output.csv]`. Optional path
// argument writes the CSV to disk; without it the report is
// stdout-only.
//
// NOTE: this file directly invokes `detail::scalar::*` and
// `detail::avx2::*` so we can compare paths side-by-side. The
// public dispatching APIs (`simd::add`, `simd::apply_transforms`,
// etc.) collapse to one of these at compile time; benching the
// public API would only ever measure one path.

#include "common.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/render/Camera.hpp>
#include <threadmaxx/render/Visibility.hpp>

#include <threadmaxx_simd/config.hpp>
#include <threadmaxx_simd/detail/scalar.hpp>
#if THREADMAXX_SIMD_HAS_AVX2
#  include <threadmaxx_simd/detail/avx2.hpp>
#endif

#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <string>
#include <vector>

using namespace threadmaxx;
using namespace threadmaxx_bench;

namespace {

constexpr int kWarmup = 4;
constexpr int kIters  = 32;

const std::vector<std::size_t> kSizes = {1024, 16 * 1024, 256 * 1024};

// Forces the compiler to keep a side-effect on a value — prevents the
// optimizer from discarding the benchmark body as dead code.
template <typename T>
inline void escape(const T& v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(&v) : "memory");
#endif
}

// Build a deterministic RNG-seeded scene of Vec3 / Quat / Transform /
// Velocity / BoundingVolume buffers, sized for the requested entity
// count. Pre-allocated once per workload so the per-iteration body
// only does the math.
struct Scene {
    std::vector<Vec3>           va, vb, vc;
    std::vector<Quat>           qa;
    std::vector<Transform>      ta;
    std::vector<Velocity>       vels;
    std::vector<Vec3>           linVels;
    std::vector<Vec3>           pts;
    std::vector<BoundingVolume> bvIn, bvOut;
    std::vector<float>          radii;
    std::vector<std::uint8_t>   mask;
    Frustum                     frustum;

    explicit Scene(std::size_t n) {
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> d(-5.0f, 5.0f);
        std::uniform_real_distribution<float> dq(-1.0f, 1.0f);
        std::uniform_real_distribution<float> dS(0.5f, 2.0f);
        std::uniform_real_distribution<float> dR(0.1f, 2.0f);

        va.resize(n); vb.resize(n); vc.resize(n);
        qa.resize(n); ta.resize(n); vels.resize(n); linVels.resize(n);
        pts.resize(n); bvIn.resize(n); bvOut.resize(n);
        radii.resize(n); mask.resize(n);

        for (std::size_t i = 0; i < n; ++i) {
            va[i] = Vec3{d(rng), d(rng), d(rng)};
            vb[i] = Vec3{d(rng), d(rng), d(rng)};
            // Pre-normalize qa to keep slerp / rotate inputs sensible.
            float qx = dq(rng), qy = dq(rng), qz = dq(rng), qw = dq(rng);
            const float len = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
            if (len > 0) { qx /= len; qy /= len; qz /= len; qw /= len; }
            else         { qx = 0; qy = 0; qz = 0; qw = 1; }
            qa[i] = Quat{qx, qy, qz, qw};
            ta[i].position    = Vec3{d(rng), d(rng), d(rng)};
            ta[i].orientation = qa[i];
            ta[i].scale       = Vec3{dS(rng), dS(rng), dS(rng)};
            vels[i].linear    = Vec3{d(rng), d(rng), d(rng)};
            vels[i].angular   = Vec3{d(rng) * 0.1f, d(rng) * 0.1f, d(rng) * 0.1f};
            linVels[i]        = vels[i].linear;
            pts[i]            = Vec3{d(rng), d(rng), d(rng)};
            const Vec3 c{d(rng), d(rng), d(rng)};
            const Vec3 e{dR(rng), dR(rng), dR(rng)};
            bvIn[i].min = Vec3{c.x - e.x, c.y - e.y, c.z - e.z};
            bvIn[i].max = Vec3{c.x + e.x, c.y + e.y, c.z + e.z};
            radii[i] = dR(rng);
        }

        // Build a typical-shaped Frustum from an axis-aligned cube
        // (10-unit half-extent). Reuses normalized-plane shape so the
        // p-vertex test path runs the same code as production.
        frustum.planes[0] = { 1, 0, 0, 10};
        frustum.planes[1] = {-1, 0, 0, 10};
        frustum.planes[2] = { 0, 1, 0, 10};
        frustum.planes[3] = { 0,-1, 0, 10};
        frustum.planes[4] = { 0, 0, 1, 10};
        frustum.planes[5] = { 0, 0,-1, 10};
    }
};

// Emit one paired (scalar, avx2) measurement, including the speedup
// ratio (column "throughput" overloaded to mean "speedup factor").
void emitPair(CsvWriter& csv, const std::string& kernel, std::size_t n,
              const LatencyHistogram& sca, const LatencyHistogram& vec) {
    BenchRow rScalar{};
    rScalar.label    = kernel + "/scalar";
    rScalar.workload = "simd";
    rScalar.entities = n;
    rScalar.mean_ns  = sca.meanNs();
    rScalar.stddev   = sca.stddev();
    rScalar.p50_ns   = sca.p50Ns();
    rScalar.p95_ns   = sca.p95Ns();
    rScalar.p99_ns   = sca.p99Ns();
    csv.row(rScalar);

    BenchRow rAvx2{};
    rAvx2.label    = kernel + "/avx2";
    rAvx2.workload = "simd";
    rAvx2.entities = n;
    rAvx2.mean_ns  = vec.meanNs();
    rAvx2.stddev   = vec.stddev();
    rAvx2.p50_ns   = vec.p50Ns();
    rAvx2.p95_ns   = vec.p95Ns();
    rAvx2.p99_ns   = vec.p99Ns();
    if (vec.meanNs() > 0) {
        rAvx2.throughput = sca.meanNs() / vec.meanNs();  // ×speedup
    }
    csv.row(rAvx2);
}

} // namespace

int main(int argc, char** argv) {
#if !THREADMAXX_SIMD_HAS_AVX2
    std::printf("[bench_simd_kernels] AVX2 not built; no comparison to make.\n");
    return 0;
#else
    namespace sca = threadmaxx::simd::detail::scalar;
    namespace vec = threadmaxx::simd::detail::avx2;

    CsvWriter csv(argc > 1 ? argv[1] : nullptr);

    for (std::size_t n : kSizes) {
        Scene s(n);

        // ---- Vec3 element-wise: add / sub / scale / madd ----------------
        {
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{ sca::add(s.va, s.vb, s.vc); escape(s.vc[0]); });
            runIters(hv, kWarmup, kIters, [&]{ vec::add(s.va, s.vb, s.vc); escape(s.vc[0]); });
            emitPair(csv, "vec3_add", n, hs, hv);
        }
        {
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{ sca::madd(s.va, s.vb, 0.5f, s.vc); escape(s.vc[0]); });
            runIters(hv, kWarmup, kIters, [&]{ vec::madd(s.va, s.vb, 0.5f, s.vc); escape(s.vc[0]); });
            emitPair(csv, "vec3_madd", n, hs, hv);
        }
        {
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{
                volatile float d = sca::dot(s.va, s.vb); (void)d; });
            runIters(hv, kWarmup, kIters, [&]{
                volatile float d = vec::dot(s.va, s.vb); (void)d; });
            emitPair(csv, "vec3_dot", n, hs, hv);
        }
        {
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{ sca::normalize(s.va, s.vc); escape(s.vc[0]); });
            runIters(hv, kWarmup, kIters, [&]{ vec::normalize(s.va, s.vc); escape(s.vc[0]); });
            emitPair(csv, "vec3_normalize", n, hs, hv);
        }

        // ---- Quat normalize ---------------------------------------------
        {
            std::vector<Quat> tmp = s.qa;
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{
                tmp = s.qa; sca::quat_normalize(std::span<Quat>(tmp));
                escape(tmp[0]);
            });
            runIters(hv, kWarmup, kIters, [&]{
                tmp = s.qa; vec::quat_normalize(std::span<Quat>(tmp));
                escape(tmp[0]);
            });
            emitPair(csv, "quat_normalize", n, hs, hv);
        }

        // ---- frustum_cull (sphere-based) --------------------------------
        {
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{
                sca::frustum_cull(s.va, s.radii, s.frustum, s.mask);
                escape(s.mask[0]);
            });
            runIters(hv, kWarmup, kIters, [&]{
                vec::frustum_cull(s.va, s.radii, s.frustum, s.mask);
                escape(s.mask[0]);
            });
            emitPair(csv, "frustum_cull", n, hs, hv);
        }

        // ---- apply_transforms -------------------------------------------
        {
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{
                sca::apply_transforms(s.ta, s.pts, s.vc); escape(s.vc[0]);
            });
            runIters(hv, kWarmup, kIters, [&]{
                vec::apply_transforms(s.ta, s.pts, s.vc); escape(s.vc[0]);
            });
            emitPair(csv, "apply_transforms", n, hs, hv);
        }

        // ---- integrate_linear_motion ------------------------------------
        {
            std::vector<Transform> work = s.ta;
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{
                work = s.ta;
                sca::integrate_linear_motion(work, s.linVels, 0.016f);
                escape(work[0]);
            });
            runIters(hv, kWarmup, kIters, [&]{
                work = s.ta;
                vec::integrate_linear_motion(work, s.linVels, 0.016f);
                escape(work[0]);
            });
            emitPair(csv, "integrate_linear_motion", n, hs, hv);
        }

        // ---- transform_aabb ---------------------------------------------
        {
            LatencyHistogram hs, hv;
            runIters(hs, kWarmup, kIters, [&]{
                sca::transform_aabb(s.ta, s.bvIn, s.bvOut);
                escape(s.bvOut[0]);
            });
            runIters(hv, kWarmup, kIters, [&]{
                vec::transform_aabb(s.ta, s.bvIn, s.bvOut);
                escape(s.bvOut[0]);
            });
            emitPair(csv, "transform_aabb", n, hs, hv);
        }
    }

    return 0;
#endif
}
