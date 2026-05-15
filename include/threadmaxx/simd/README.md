# `threadmaxx/simd` — sibling SIMD helper library

## 1. Purpose

`threadmaxx/simd` provides **optional vectorized batch kernels** for data already stored in contiguous chunk arrays by `threadmaxx`.

It is for:

* movement and integration loops,
* bulk transform updates,
* quaternion/vector math on spans,
* AABB, frustum, and matrix helper kernels,
* skinning or animation-side batch math in game code.

It is **not** for:

* owning memory,
* replacing the core math types,
* changing engine storage layout,
* implementing a general-purpose math library,
* forcing any particular ISA on users.

## 2. Design principles

1. **Header-only.** Include only what you use.
2. **Optional.** The core engine must compile and run without this library.
3. **No core ABI changes.** `threadmaxx` keeps its POD layout types as-is.
4. **Span-first API.** Every hot path operates on `std::span` or raw contiguous ranges.
5. **Zero allocation.** No heap use inside kernels.
6. **Scalar fallback always exists.** SIMD is an optimization, not a requirement.
7. **Architecture-specific code stays isolated.** AVX2 / NEON / SSE2 are implementation details.
8. **No reinterpret_cast contracts in user code.** Use explicit views and trait checks.
9. **Only contiguous data.** This library assumes the chunked storage model from `forEachChunk`.
10. **Small surface area.** A few good kernels beat a giant math namespace.

## 3. Package layout

```text
include/threadmaxx/simd/
  simd.hpp            // umbrella include
  config.hpp          // feature detection and dispatch
  traits.hpp          // trivially-copyable / layout checks
  views.hpp           // span adapters for core PODs
  lanes.hpp           // lane-count helpers and batch utilities
  vec3_ops.hpp        // batch vector kernels
  quat_ops.hpp        // batch quaternion kernels
  transform_ops.hpp   // batch transform kernels
  aabb_ops.hpp        // AABB and bounds kernels
  simd_math.hpp       // small utilities: reciprocal, rsqrt, clamp, min/max
  cpu.hpp             // optional one-time CPU feature probe
  detail/
    scalar.hpp
    avx2.hpp
    sse2.hpp
    neon.hpp
```

## 4. What the library exports

### 4.1 Feature/config API

```cpp
namespace threadmaxx::simd {

enum class isa {
    scalar,
    sse2,
    avx2,
    neon
};

struct capabilities {
    bool scalar = true;
    bool sse2   = false;
    bool avx2   = false;
    bool neon   = false;
};

constexpr capabilities compile_time_capabilities() noexcept;
capabilities runtime_capabilities() noexcept;   // optional, cheap once-initialized probe

isa preferred_isa() noexcept;
} // namespace threadmaxx::simd
```

This should be used only for dispatch and diagnostics. It should not leak into gameplay code.

### 4.2 Layout traits

The library should accept only types that are safe to process as contiguous arrays.

```cpp
template<class T>
concept simd_batchable =
    std::is_trivially_copyable_v<T> &&
    std::is_standard_layout_v<T> &&
    (alignof(T) <= alignof(std::max_align_t));
```

Do **not** require `alignas(32)` on core types. The library should work with the engine’s existing POD types and with user-defined PODs that satisfy the trait.

### 4.3 View adapters

The key abstraction is not a new vector type; it is a view over contiguous arrays.

```cpp
namespace threadmaxx::simd {

template<class T>
struct span_view {
    std::span<T> values;

    T* data() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
};

template<class T>
span_view<T> view(std::span<T> s) noexcept;

template<class T>
span_view<const T> view(std::span<const T> s) noexcept;

} // namespace threadmaxx::simd
```

For `threadmaxx` chunk data, the intended use is:

```cpp
ctx.forEachChunk<Transform, Velocity>(
    [](auto& chunk) {
        auto t = threadmaxx::simd::view(chunk.span<Transform>());
        auto v = threadmaxx::simd::view(chunk.span<Velocity>());
        threadmaxx::simd::integrate_positions(t, v, dt);
    }
);
```

## 5. Kernel API

Keep the API focused on the operations that matter for game workloads.

### 5.1 Vector kernels

```cpp
namespace threadmaxx::simd {

void add(std::span<const Vec3> a,
         std::span<const Vec3> b,
         std::span<Vec3> out) noexcept;

void sub(std::span<const Vec3> a,
         std::span<const Vec3> b,
         std::span<Vec3> out) noexcept;

void scale(std::span<const Vec3> in,
           float s,
           std::span<Vec3> out) noexcept;

void madd(std::span<const Vec3> a,
          std::span<const Vec3> b,
          float s,
          std::span<Vec3> out) noexcept;

void normalize(std::span<const Vec3> in,
               std::span<Vec3> out) noexcept;

float dot(std::span<const Vec3> a,
          std::span<const Vec3> b) noexcept;
}
```

### 5.2 Transform kernels

```cpp
namespace threadmaxx::simd {

void apply_transforms(std::span<const Transform> t,
                      std::span<const Vec3> points,
                      std::span<Vec3> out) noexcept;

void integrate_positions(std::span<Transform> t,
                         std::span<const Velocity> v,
                         float dt) noexcept;

void integrate_linear_motion(std::span<Transform> t,
                             std::span<const Vec3> velocity,
                             float dt) noexcept;
}
```

### 5.3 Quaternion and bounds kernels

```cpp
namespace threadmaxx::simd {

void normalize(std::span<Quat> q) noexcept;
void slerp(std::span<const Quat> a,
           std::span<const Quat> b,
           std::span<Quat> out,
           float alpha) noexcept;

void transform_aabb(std::span<const Transform> t,
                    std::span<const AABB> in,
                    std::span<AABB> out) noexcept;

void frustum_cull(std::span<const Vec3> centers,
                  std::span<const float> radii,
                  const Frustum& frustum,
                  std::span<std::uint8_t> visible_mask) noexcept;
}
```

## 6. Dispatch model

The dispatch strategy should be simple:

* compile-time selection when the target ISA is known,
* optional one-time runtime dispatch when multiple code paths are built,
* scalar fallback always available,
* no per-element branching.

Recommended shape:

```cpp
namespace threadmaxx::simd::detail {

template<class FScalar, class FSse2, class FAvx2, class FNeon>
decltype(auto) dispatch(FScalar&& scalar,
                        FSse2&& sse2,
                        FAvx2&& avx2,
                        FNeon&& neon) noexcept;

} // namespace threadmaxx::simd::detail
```

This keeps the decision in one place and keeps public kernels clean.

## 7. Integration with `threadmaxx`

The core integration point is `forEachChunk`.

The library should assume:

* the data comes from contiguous chunk spans,
* the loop body owns the writeback,
* the caller decides which kernels to use,
* no engine internals need to change.

Example:

```cpp
ctx.forEachChunk<Transform, Velocity>(
    [&](auto& chunk) {
        auto transforms = chunk.span<Transform>();
        auto velocities  = chunk.span<Velocity>();

        threadmaxx::simd::integrate_positions(
            transforms,
            velocities,
            ctx.deltaTime()
        );
    }
);
```

That fits the roadmap’s chunked-storage model directly. The engine already provides contiguous arrays as the SIMD enabler. 

## 8. Core types remain unchanged

Do **not** change `threadmaxx::Vec3`, `Quat`, or `Transform` to make SIMD easier.

Instead:

* keep those as engine PODs for layout compatibility,
* let `threadmaxx/simd` adapt them,
* allow games that already use another math library to keep using it,
* avoid forcing a single “engine math style” on everyone. 

That is directly aligned with the roadmap’s rule that the engine should ship layout compatibility, not arithmetic ownership. 

## 9. Non-goals

The following stay out of scope:

* a full matrix library,
* inverse kinematics,
* cloth,
* animation blending trees,
* physics solver math,
* memory allocation,
* new component types,
* new storage formats,
* mandatory SIMD on all platforms,
* public reliance on `reinterpret_cast`.

If a feature only makes sense as a game system, it belongs above the engine, just like the roadmap says for animation, physics, networking, and navmesh. 

## 10. Implementation order

1. `config.hpp` and `traits.hpp`
2. `views.hpp`
3. scalar kernels
4. one backend, probably AVX2
5. NEON or SSE2 as a second backend
6. transform and integration kernels
7. bounds and culling kernels
8. benchmarks and correctness tests

## 11. Tests to add

* scalar vs SIMD equality tests for each kernel,
* alignment and trait rejection tests,
* chunk-span integration test with `forEachChunk`,
* mixed-size tail handling tests,
* random-data fuzz tests for transforms and normalization,
* benchmark comparison against scalar loops.
