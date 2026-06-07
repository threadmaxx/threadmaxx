#include "Check.hpp"
#include "threadmaxx_animation/ik.hpp"

#include <vector>

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-3f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-3f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

}  // namespace

int main() {
    // 3-joint chain bent up. Target = (1, 0, 0). At weight=1 the chain
    // bends further. At weight=0.5 the result must be the midpoint
    // between the input chain and the fully-solved chain. At weight=0
    // the chain stays put.
    const std::vector<Vec3> initial = {
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{1.0f, 0.5f, 0.0f},
        Vec3{2.0f, 0.0f, 0.0f},
    };

    IKTarget tFull;
    tFull.position = Vec3{1.0f, 0.0f, 0.0f};
    tFull.maxIterations = 64;
    tFull.tolerance = 1e-6f;
    tFull.weight = 1.0f;
    std::vector<Vec3> chainFull = initial;
    solveIK(std::span<Vec3>(chainFull), tFull);

    // 1. weight=0.5 → output is the midpoint of (initial, chainFull).
    {
        IKTarget t = tFull;
        t.weight = 0.5f;
        std::vector<Vec3> chainHalf = initial;
        solveIK(std::span<Vec3>(chainHalf), t);
        for (std::size_t i = 0; i < initial.size(); ++i) {
            const Vec3 expected{
                0.5f * initial[i].x + 0.5f * chainFull[i].x,
                0.5f * initial[i].y + 0.5f * chainFull[i].y,
                0.5f * initial[i].z + 0.5f * chainFull[i].z,
            };
            CHECK(nearly(chainHalf[i], expected));
        }
    }

    // 2. weight=0.0 → output is the input chain unchanged.
    {
        IKTarget t = tFull;
        t.weight = 0.0f;
        std::vector<Vec3> chainZero = initial;
        solveIK(std::span<Vec3>(chainZero), t);
        for (std::size_t i = 0; i < initial.size(); ++i) {
            CHECK(nearly(chainZero[i], initial[i]));
        }
    }

    // 3. weight=0.25 → 75% initial + 25% solved.
    {
        IKTarget t = tFull;
        t.weight = 0.25f;
        std::vector<Vec3> chainQ = initial;
        solveIK(std::span<Vec3>(chainQ), t);
        for (std::size_t i = 0; i < initial.size(); ++i) {
            const Vec3 expected{
                0.75f * initial[i].x + 0.25f * chainFull[i].x,
                0.75f * initial[i].y + 0.25f * chainFull[i].y,
                0.75f * initial[i].z + 0.25f * chainFull[i].z,
            };
            CHECK(nearly(chainQ[i], expected));
        }
    }

    EXIT_WITH_RESULT();
}
