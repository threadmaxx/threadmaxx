#include "threadmaxx_animation/ik.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace threadmaxx::animation {

namespace {

float length(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

float distance(const Vec3& a, const Vec3& b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Vec3 normalizeOr(const Vec3& v, const Vec3& fallback) noexcept {
    const float n = length(v);
    if (n < 1e-8f) {
        return fallback;
    }
    const float inv = 1.0f / n;
    return Vec3{v.x * inv, v.y * inv, v.z * inv};
}

Vec3 lerp3(const Vec3& a, const Vec3& b, float t) noexcept {
    const float oneMinus = 1.0f - t;
    return Vec3{
        oneMinus * a.x + t * b.x,
        oneMinus * a.y + t * b.y,
        oneMinus * a.z + t * b.z,
    };
}

float dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

}  // namespace

IKSolveResult solveIK(std::span<Vec3> positions, const IKTarget& target) noexcept {
    IKSolveResult result;
    const std::size_t n = positions.size();
    if (n < 2) {
        result.converged = true;
        result.finalDistance = 0.0f;
        return result;
    }

    // Snapshot the input positions so a partial-weight blend can lerp
    // toward them after the solve.
    std::vector<Vec3> original(positions.begin(), positions.end());

    // Precompute bone lengths and total reach. boneLengths[i] is the
    // distance from joint i to joint i+1.
    std::vector<float> boneLengths(n - 1);
    float totalLength = 0.0f;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        boneLengths[i] = distance(original[i], original[i + 1]);
        totalLength += boneLengths[i];
    }

    const Vec3 root = original[0];
    const Vec3 targetPos = target.position;
    const float rootToTarget = distance(root, targetPos);

    if (rootToTarget > totalLength) {
        // Unreachable: lay the chain out along (target - root) at
        // the original bone lengths. End-effector falls short of the
        // target by (rootToTarget - totalLength).
        const Vec3 dir = normalizeOr(Vec3{targetPos.x - root.x,
                                          targetPos.y - root.y,
                                          targetPos.z - root.z},
                                     Vec3{1.0f, 0.0f, 0.0f});
        positions[0] = root;
        for (std::size_t i = 1; i < n; ++i) {
            positions[i] = Vec3{
                positions[i - 1].x + dir.x * boneLengths[i - 1],
                positions[i - 1].y + dir.y * boneLengths[i - 1],
                positions[i - 1].z + dir.z * boneLengths[i - 1],
            };
        }
        result.converged = false;
        result.iterations = 0;
        result.finalDistance = rootToTarget - totalLength;
    } else {
        // Reachable: FABRIK forward + backward sweep until the
        // end-effector is within tolerance of the target.
        const std::uint32_t maxIters = target.maxIterations;
        std::uint32_t iter = 0;
        float endDist = distance(positions[n - 1], targetPos);
        for (; iter < maxIters && endDist > target.tolerance; ++iter) {
            // Forward reaching: end-effector ← target; walk backward
            // placing each joint at boneLength from its child.
            positions[n - 1] = targetPos;
            for (std::size_t i = n - 1; i > 0; --i) {
                const Vec3 toParent{
                    positions[i - 1].x - positions[i].x,
                    positions[i - 1].y - positions[i].y,
                    positions[i - 1].z - positions[i].z,
                };
                const Vec3 dir = normalizeOr(toParent, Vec3{1.0f, 0.0f, 0.0f});
                positions[i - 1] = Vec3{
                    positions[i].x + dir.x * boneLengths[i - 1],
                    positions[i].y + dir.y * boneLengths[i - 1],
                    positions[i].z + dir.z * boneLengths[i - 1],
                };
            }
            // Backward reaching: root ← original root; walk forward
            // placing each child at boneLength from its parent.
            positions[0] = root;
            for (std::size_t i = 0; i + 1 < n; ++i) {
                const Vec3 toChild{
                    positions[i + 1].x - positions[i].x,
                    positions[i + 1].y - positions[i].y,
                    positions[i + 1].z - positions[i].z,
                };
                const Vec3 dir = normalizeOr(toChild, Vec3{1.0f, 0.0f, 0.0f});
                positions[i + 1] = Vec3{
                    positions[i].x + dir.x * boneLengths[i],
                    positions[i].y + dir.y * boneLengths[i],
                    positions[i].z + dir.z * boneLengths[i],
                };
            }
            endDist = distance(positions[n - 1], targetPos);
        }
        result.iterations = iter;
        result.converged = endDist <= target.tolerance;
        result.finalDistance = endDist;
    }

    // Partial-weight blend toward the input chain. Bone lengths are
    // not re-projected onto the lerp result by design — the caller
    // wants a fractional pull, not a constrained intermediate pose.
    if (target.weight < 1.0f) {
        const float w = (target.weight < 0.0f) ? 0.0f : target.weight;
        for (std::size_t i = 0; i < n; ++i) {
            positions[i] = lerp3(original[i], positions[i], w);
        }
        result.finalDistance = distance(positions[n - 1], targetPos);
    }

    return result;
}

Vec3 solve2BoneIK(const Vec3& shoulder,
                  const Vec3& wristTarget,
                  const Vec3& pole,
                  float upperLength,
                  float lowerLength) noexcept {
    const Vec3 toTarget{
        wristTarget.x - shoulder.x,
        wristTarget.y - shoulder.y,
        wristTarget.z - shoulder.z,
    };
    const float d = length(toTarget);
    const Vec3 axis = normalizeOr(toTarget, Vec3{1.0f, 0.0f, 0.0f});

    // Unreachable: elbow at upperLength along the shoulder→target line.
    if (d >= upperLength + lowerLength) {
        return Vec3{
            shoulder.x + axis.x * upperLength,
            shoulder.y + axis.y * upperLength,
            shoulder.z + axis.z * upperLength,
        };
    }
    // Folded case: target closer than |upperLength - lowerLength| from
    // the shoulder. Elbow goes back along the opposite axis at distance
    // upperLength from the shoulder.
    const float absDelta = upperLength > lowerLength
                               ? upperLength - lowerLength
                               : lowerLength - upperLength;
    if (d <= absDelta) {
        return Vec3{
            shoulder.x - axis.x * upperLength,
            shoulder.y - axis.y * upperLength,
            shoulder.z - axis.z * upperLength,
        };
    }

    // Law of cosines for the angle at the shoulder between the
    // shoulder→target axis and the shoulder→elbow axis.
    //   cos(α) = (upper² + d² - lower²) / (2 · upper · d)
    const float cosAlpha = (upperLength * upperLength + d * d -
                            lowerLength * lowerLength) /
                           (2.0f * upperLength * d);
    const float clampedCos = (cosAlpha > 1.0f) ? 1.0f
                              : (cosAlpha < -1.0f) ? -1.0f
                              : cosAlpha;
    const float sinAlpha = std::sqrt(1.0f - clampedCos * clampedCos);

    // Build a stable bend-plane normal from the pole hint. Project
    // (pole - shoulder) into the plane perpendicular to `axis`, then
    // cross it with `axis` to get the bend direction.
    const Vec3 poleVec{
        pole.x - shoulder.x,
        pole.y - shoulder.y,
        pole.z - shoulder.z,
    };
    const float poleAxis = dot(poleVec, axis);
    Vec3 bendDir{
        poleVec.x - axis.x * poleAxis,
        poleVec.y - axis.y * poleAxis,
        poleVec.z - axis.z * poleAxis,
    };
    // If pole is collinear with axis, pick an arbitrary stable axis.
    Vec3 bend = normalizeOr(bendDir, Vec3{0.0f, 0.0f, 0.0f});
    if (bend.x == 0.0f && bend.y == 0.0f && bend.z == 0.0f) {
        // Choose the world axis least aligned with `axis`.
        const Vec3 candidate = (std::abs(axis.x) < 0.9f)
                                   ? Vec3{1.0f, 0.0f, 0.0f}
                                   : Vec3{0.0f, 1.0f, 0.0f};
        const float candAxis = dot(candidate, axis);
        bend = normalizeOr(Vec3{
            candidate.x - axis.x * candAxis,
            candidate.y - axis.y * candAxis,
            candidate.z - axis.z * candAxis,
        }, Vec3{0.0f, 1.0f, 0.0f});
    }
    return Vec3{
        shoulder.x + upperLength * (clampedCos * axis.x + sinAlpha * bend.x),
        shoulder.y + upperLength * (clampedCos * axis.y + sinAlpha * bend.y),
        shoulder.z + upperLength * (clampedCos * axis.z + sinAlpha * bend.z),
    };
}

}  // namespace threadmaxx::animation
