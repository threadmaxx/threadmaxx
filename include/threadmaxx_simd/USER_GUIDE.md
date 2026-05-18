# `threadmaxx_simd` — User Guide

Header-only batch SIMD kernels for the `threadmaxx` engine's PODs.
Span-based, no-allocation, scalar-fallback-always.

## When to use this library

Reach for `threadmaxx_simd` when you have **contiguous arrays of
trivially-copyable POD types** and want to operate on them in bulk:

- Per-tick integration over thousands of `Transform`s.
- Frustum culling over a frame's bounding spheres.
- Per-vertex quaternion rotations during instance buffer
  preparation.
- Dot products / vector arithmetic over particle streams.

If you have one entity at a time, don't bother — the scalar
implementations are still optimal at small grain (see "When AVX2
loses" below).

## Quick start

```cpp
#include <threadmaxx_simd/threadmaxx_simd.hpp>  // umbrella
// or, more granular:
//   #include <threadmaxx_simd/vec3_ops.hpp>
//   #include <threadmaxx_simd/cpu.hpp>

#include <threadmaxx/Components.hpp>

#include <span>
#include <vector>

void integrate(std::span<threadmaxx::Transform> transforms,
               std::span<const threadmaxx::Vec3> velocities,
               float dt) {
    threadmaxx::simd::integrate_linear_motion(transforms, velocities, dt);
}
```

That's it. The library handles per-element math, tail-size
handling, and backend selection internally. The same call site
produces optimal code whether the build target is plain `-O2`
(scalar fallback) or `-O2 -mavx2 -mfma` (AVX2 paths).

## Build setup

Add the dependency:

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::simd)
```

The CMake target carries the include directory and a transitive
link to `threadmaxx::threadmaxx` (the SIMD kernels work on engine
POD types). No object files are produced — it's an INTERFACE
target.

### Enabling AVX2 (recommended on modern x86_64)

The library is portable by default — your build sees only the
scalar paths. To enable AVX2 dispatch, add the compiler flags to
**your** target:

```cmake
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT MSVC)
    target_compile_options(my_target PRIVATE -mavx2 -mfma)
endif()
```

The library is `#if`-gated on `__AVX2__` and `__FMA__` (defined
automatically by the compiler when those flags are present). Your
production binary then uses AVX2 paths where they win; the scalar
fallback compiles out.

### Build option

The library itself is opt-out via:

```
cmake -B build -DTHREADMAXX_BUILD_SIMD=ON  # default ON
```

Setting `OFF` drops the `threadmaxx::simd` target so consumers
fail to find it — useful only if you want to assert the engine
builds standalone.

## Kernel inventory

All kernels live in namespace `threadmaxx::simd`. Each accepts
`std::span` (or `span_view<T>` — see "View abstraction" below)
and is `noexcept`.

### Vector kernels (`vec3_ops.hpp`)

| Kernel                            | Effect                                  | Dispatch  |
|-----------------------------------|-----------------------------------------|-----------|
| `add(a, b, out)`                  | `out[i] = a[i] + b[i]`                  | AVX2 ✓    |
| `sub(a, b, out)`                  | `out[i] = a[i] - b[i]`                  | AVX2 ✓    |
| `scale(in, s, out)`               | `out[i] = in[i] * s`                    | AVX2 ✓    |
| `madd(a, b, s, out)`              | `out[i] = a[i] + b[i] * s`              | AVX2 ✓    |
| `normalize(in, out)`              | `out[i] = unit(in[i])`                  | scalar    |
| `dot(a, b) -> float`              | Σᵢ `a[i] · b[i]`                       | AVX2 ✓    |

### Quaternion kernels (`quat_ops.hpp`)

| Kernel                            | Effect                                  | Dispatch  |
|-----------------------------------|-----------------------------------------|-----------|
| `normalize(q)`                    | In-place; zero-norm → identity          | scalar    |
| `slerp(a, b, out, alpha)`         | Shortest-path SLERP                     | scalar    |
| `nlerp(a, b, out, alpha)`         | Normalized-linear interp (fast slerp)   | scalar    |

### Transform kernels (`transform_ops.hpp`)

| Kernel                                       | Effect                                            | Dispatch  |
|----------------------------------------------|---------------------------------------------------|-----------|
| `apply_transforms(t, points, out)`           | `out[i] = t[i].pos + rotate(q, scale·points[i])` | AVX2 ✓    |
| `integrate_positions(t, v, dt)`              | In-place; linear + angular                        | scalar    |
| `integrate_linear_motion(t, vel, dt)`        | In-place; position only                           | scalar    |

### AABB / Frustum kernels (`aabb_ops.hpp`)

| Kernel                                       | Effect                                            | Dispatch  |
|----------------------------------------------|---------------------------------------------------|-----------|
| `transform_aabb(t, in, out)`                 | World-space axis-aligned bound of oriented AABB   | AVX2 ✓    |
| `frustum_cull(centers, radii, frustum, mask)`| `mask[i] = 1` if sphere intersects frustum        | AVX2 ✓    |

### Why some kernels are scalar-dispatched

Several kernels have working AVX2 implementations in
`detail/avx2.hpp` (covered by equivalence tests) but the public
dispatcher routes them to scalar because the benchmark
(`bench/simd_kernels`) showed scalar is faster on the host's
workload. See "When AVX2 loses" below for the underlying reasons.

## Feature detection

### Compile-time

```cpp
#include <threadmaxx_simd/config.hpp>

constexpr auto caps = threadmaxx::simd::compile_time_capabilities();
static_assert(caps.scalar);    // always true
if constexpr (caps.avx2) {
    // The current TU was built with AVX2.
}
```

`preferred_isa()` returns the highest-supported ISA:

```cpp
constexpr auto isa = threadmaxx::simd::preferred_isa();
// One of: isa::scalar, isa::sse2, isa::avx2, isa::neon
```

### Runtime (CPUID probe)

```cpp
#include <threadmaxx_simd/cpu.hpp>

const auto rt = threadmaxx::simd::runtime_capabilities();
if (rt.avx2) {
    log("CPU supports AVX2");
}
```

`runtime_capabilities()` is cached after the first call (C++11
magic statics; thread-safe). On x86 it queries CPUID; on ARM it
honestly reports the compile-time flag (no portable runtime NEON
probe).

Why two views? The runtime view is useful for fat-binary scenarios
or diagnostic logging ("running scalar fallback because this CPU
lacks AVX2"). For single-target builds, the compile-time view
matches the runtime view, and you don't need either — just call
the public kernels.

## View abstraction

Every kernel has two overloads: one taking `std::span<T>` and one
taking `span_view<T>`. The latter is a thin adapter recommended
by `DESIGN_NOTES.md`:

```cpp
auto va = threadmaxx::simd::view(std::span<const Vec3>(myVec3s));
auto vb = threadmaxx::simd::view(std::span<const Vec3>(otherVec3s));
auto vo = threadmaxx::simd::view(std::span<Vec3>(outBuffer));
threadmaxx::simd::add(va, vb, vo);
```

`span_view` is `static_assert`-checked against the `simd_batchable`
concept at the boundary, giving you a compile-time error if you
hand it a non-POD or over-aligned type. The `std::span` overloads
do the same check lazily. Pick whichever feels more natural — the
generated code is identical.

## Integration with `forEachChunk`

The library's main intended use is alongside the engine's chunk
iteration (`DESIGN_NOTES.md` §7):

```cpp
class IntegratorSystem : public threadmaxx::ISystem {
public:
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }

    void update(threadmaxx::SystemContext& ctx) override {
        const float dt = ctx.dt();
        threadmaxx::forEachChunk<Transform, Velocity>(ctx,
            [dt](std::span<const EntityHandle> es,
                 std::span<const Transform>    ts,
                 std::span<const Velocity>     vs,
                 threadmaxx::CommandBuffer&    cb) {
                // Per-chunk scratch on the worker's stack.
                std::vector<Transform> nextT(ts.begin(), ts.end());
                std::vector<Vec3>      linV;
                linV.reserve(vs.size());
                for (const auto& v : vs) linV.push_back(v.linear);

                threadmaxx::simd::integrate_linear_motion(
                    nextT, linV, dt);

                for (std::size_t i = 0; i < es.size(); ++i) {
                    cb.setTransform(es[i], nextT[i]);
                }
            });
    }
};
```

Each worker gets its own stack-frame scratch — race-free by
construction. The same pattern works for any kernel that takes
parallel spans.

See `tests/simd/test_simd_chunk_integration.cpp` for a complete
runnable example.

## Conventions

### Tail / mismatched-length policy

- **Writers** (`add`, `sub`, `scale`, `madd`, `normalize`, etc.)
  stop at `min(input_sizes..., output_size)`. No exception is
  thrown for size mismatch.
- **Reductions** (`dot`) accumulate across the shorter span;
  empty input returns `0.0f`.
- **In-place kernels** (`quat_ops::normalize`,
  `integrate_*`) modify their input span directly.

### In-place aliasing

The element-wise Vec3 kernels (`add` / `sub` / `scale` / `madd`)
are safe to call with overlapping spans:

```cpp
simd::add(buf, buf, buf);  // OK: buf[i] = buf[i] + buf[i] = 2*buf[i]
```

Each iteration's reads complete before its store, in both scalar
and AVX2 paths. The other kernels (`apply_transforms`,
`transform_aabb`, etc.) read multiple input fields per element
and should not be called with input/output spans that alias.

### Zero / NaN handling

- `normalize(Vec3)` of the zero vector returns zero (no NaN).
- `normalize(Quat)` of the zero quaternion returns the identity
  `(0,0,0,1)`.
- NaN inputs propagate. The library doesn't sanitize them.

### Empty spans

All kernels accept empty spans; writers no-op, `dot` returns
`0.0f`. No special handling needed at call sites.

## Performance expectations

These are measured on a recent x86_64 desktop with `-O3 -mavx2
-mfma`, 256k entities (medians from `bench/simd_kernels`):

| Kernel                | Scalar     | AVX2       | Speedup |
|-----------------------|-----------:|-----------:|--------:|
| `vec3_dot`            | 864 µs     | 242 µs     | **3.57×** |
| `frustum_cull`        | 2,499 µs   | 709 µs     | **3.53×** |
| `transform_aabb`      | 22,739 µs  | 6,744 µs   | **3.37×** |
| `apply_transforms`    | 3,300 µs   | 2,676 µs   |   1.23× |
| `vec3_add`            | 476 µs     | 406 µs     |   1.17× |
| `vec3_madd`           | 437 µs     | 405 µs     |   1.08× |

### When AVX2 loses

Three kernels have AVX2 implementations that the dispatcher
deliberately skips:

| Kernel                       | AVX2 vs. scalar | Why                                  |
|------------------------------|----------------:|--------------------------------------|
| `vec3_normalize`             | 0.71× ❌       | Gather-based AoS↔SoA dominates       |
| `quat_normalize`             | 0.81× ❌       | `_mm256_dp_ps` latency > savings     |
| `integrate_linear_motion`    | 0.94× ❌       | Transform-stride gather ≈ scalar     |

The AVX2 code remains in `detail/avx2.hpp` and is correctness-
gated by the equivalence test, in case a future micro-opt
(permute-based deinterleave, `_mm_hadd_ps` per-quat, SoA-shaped
Transform storage in the engine) flips the trade-off.

## Running the benchmarks yourself

```
cmake -B build -DTHREADMAXX_BUILD_BENCHMARKS=ON
cmake --build build -j --target simd_kernels
./build/bench/simd_kernels                # stdout-only
./build/bench/simd_kernels out.csv        # also write CSV
```

CSV columns are documented in `bench/common.hpp`. The
"throughput" column is overloaded to mean "speedup factor of AVX2
over scalar" for SIMD bench rows.

## Restrictions / non-goals

Per `DESIGN_NOTES.md` §9, the library does NOT:

- Allocate memory inside kernels.
- Own any data — all I/O is via `std::span`.
- Provide a full math library (no matrix ops, IK, cloth, physics,
  animation blending).
- Force any particular ISA on users.
- Expose `reinterpret_cast` in its public API.
- Change the engine's POD layouts.

If you need any of the above, build it as a separate sibling
library above `threadmaxx_simd`.

## Choosing `slerp` vs `nlerp`

| Use case                            | Pick      | Why                                |
|-------------------------------------|-----------|------------------------------------|
| Adjacent animation keyframes        | `nlerp`   | Eye can't see the difference; ~3× faster |
| Long-arc camera interpolation       | `slerp`   | Constant angular velocity matters  |
| Per-frame skinning pose blends      | `nlerp`   | Throughput trumps mathematical purity |
| Physics-driven orientation drift    | `slerp`   | Predictability for downstream solvers |

Both pick the shortest path automatically (negating one input
when `dot < 0`). `nlerp` skips the `sin` / `cos` / `acos` calls
that dominate slerp's cost — substitute it freely wherever the
animation tooling defaults to slerp out of habit.

## Library version

```cpp
#include <threadmaxx_simd/version.hpp>

// Compile-time:
static_assert(THREADMAXX_SIMD_VERSION_MAJOR == 1);
#if THREADMAXX_SIMD_VERSION >= 10100  // require ≥ 1.1.0
   // ...
#endif

// Runtime:
std::printf("threadmaxx_simd v%s\n",
            threadmaxx::simd::version_string());
```

Version bumps follow [semver](https://semver.org/). See
`CHANGELOG.md` for the release history and
`MAINTAINER_GUIDE.md` for the full lifecycle policy.

## See also

- `README.md` — top-level overview.
- `DESIGN_NOTES.md` — the original spec.
- `FUTURE_WORK.md` — batch-by-batch landed work + v1.x candidates.
- `CHANGELOG.md` — per-release notes.
- `MAINTAINER_GUIDE.md` — how the dispatch / backends / tests are
  organized internally; how to add new kernels.
- `tests/simd/*.cpp` — example usage of every public API.
- `bench/simd_kernels.cpp` — perf-comparison harness.
