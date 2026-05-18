// threadmaxx_simd — `simd_batchable` concept tests.
//
// Verifies:
//   1. Every engine POD the library is meant to operate on satisfies
//      `simd_batchable` — Vec3, Quat, Transform, Velocity,
//      BoundingVolume, Acceleration.
//   2. Non-trivially-copyable types are rejected (std::string has a
//      destructor + heap pointer; it's NOT trivially copyable).
//   3. Over-aligned types are rejected. We synthesize one with
//      `alignas(64)` (one cache line, definitely stricter than
//      `std::max_align_t`).
//   4. Custom user PODs (game code) that happen to satisfy the
//      requirements ARE accepted.

#include "Check.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx_simd/traits.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

// User-style POD that should pass.
struct UserPodGood {
    float a, b, c;
    std::uint32_t flags;
};

// Over-aligned POD — should fail the alignment clause.
struct alignas(64) UserPodOverAligned {
    float a, b, c;
};

// Non-trivially-copyable type — should fail the trivially-copyable
// clause.
struct UserPodNonTrivial {
    UserPodNonTrivial() = default;
    UserPodNonTrivial(const UserPodNonTrivial&) {}  // user-defined copy ctor
    ~UserPodNonTrivial() {}                          // user-defined dtor
    float a, b, c;
};

} // namespace

int main() {
    using threadmaxx::simd::simd_batchable;

    // ---- 1. Engine PODs all pass ----------------------------------------
    static_assert(simd_batchable<threadmaxx::Vec3>,
        "Vec3 must be simd_batchable");
    static_assert(simd_batchable<threadmaxx::Quat>,
        "Quat must be simd_batchable");
    static_assert(simd_batchable<threadmaxx::Transform>,
        "Transform must be simd_batchable");
    static_assert(simd_batchable<threadmaxx::Velocity>,
        "Velocity must be simd_batchable");
    static_assert(simd_batchable<threadmaxx::Acceleration>,
        "Acceleration must be simd_batchable");
    static_assert(simd_batchable<threadmaxx::BoundingVolume>,
        "BoundingVolume must be simd_batchable");
    std::printf("[simd_traits] engine PODs all batchable OK\n");

    // ---- 2. Non-trivially-copyable rejected -----------------------------
    static_assert(!simd_batchable<std::string>,
        "std::string is not trivially copyable");
    static_assert(!simd_batchable<UserPodNonTrivial>,
        "user-defined dtor disqualifies POD");
    std::printf("[simd_traits] non-trivial types rejected OK\n");

    // ---- 3. Over-aligned rejected ---------------------------------------
    static_assert(!simd_batchable<UserPodOverAligned>,
        "alignas(64) > alignof(max_align_t) on common platforms");
    std::printf("[simd_traits] over-aligned types rejected OK\n");

    // ---- 4. User-defined PODs accepted -----------------------------------
    static_assert(simd_batchable<UserPodGood>,
        "well-behaved user POD must be batchable");
    std::printf("[simd_traits] well-behaved user PODs accepted OK\n");

    // Plain primitive types should also satisfy — kernels working on
    // raw floats / ints are explicitly within scope of the library.
    static_assert(simd_batchable<float>,  "float must be batchable");
    static_assert(simd_batchable<int>,    "int must be batchable");

    EXIT_WITH_RESULT();
}
