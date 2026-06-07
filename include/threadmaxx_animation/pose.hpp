#pragma once

#include "threadmaxx/Components.hpp"  // Vec3, Quat

#include <cstddef>
#include <span>
#include <vector>

/// Pose data model. A pose is a per-joint TRS transform plus optional
/// metadata. `JointPose` is the storage atom; `Pose` is an owning
/// buffer with a weight + valid flag; `PoseSpan` is a non-owning view
/// (the contract handed to the renderer / IK / blend kernels);
/// `PoseBuffer` is the caller-owned reusable buffer used in the hot
/// evaluation path so no allocation occurs per-tick.
///
/// AoS by deliberate choice for v1.0 — the SIMD-accelerated blend
/// kernels (A1.x roadmap) operate over `PoseSpan` and may flip to
/// SoA internally without changing the public surface.
namespace threadmaxx::animation {

/// Re-exports of the core math PODs so consumers that
/// `using namespace threadmaxx::animation` see Vec3 / Quat without a
/// second using-declaration. Same byte layout — these are aliases,
/// not new types.
using Vec3 = ::threadmaxx::Vec3;
using Quat = ::threadmaxx::Quat;

/// Per-joint local transform. Scale defaults to identity. 40 bytes
/// (12 + 16 + 12) — trivially-copyable, SIMD-friendly once gathered
/// into `std::span<JointPose>` views.
struct JointPose {
    Vec3 translation{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

/// Owning pose buffer with weight + validity metadata. Useful as a
/// stable snapshot or a heap-allocated graph node output.
struct Pose {
    std::vector<JointPose> joints;
    float weight = 1.0f;
    bool valid = false;
};

/// Non-owning span over a contiguous JointPose run — the type passed
/// into kernels (blend, IK, warp). Caller guarantees the underlying
/// storage outlives the span.
struct PoseSpan {
    std::span<JointPose> joints;
};

/// Reusable caller-owned pose storage. Reserve once, resize as the
/// skeleton joint count changes, hot-path evaluation writes into
/// `localPose()` with no allocation.
class PoseBuffer {
public:
    PoseBuffer() = default;
    explicit PoseBuffer(std::size_t jointCount)
        : joints_(jointCount) {}

    void resize(std::size_t jointCount) { joints_.resize(jointCount); }
    std::size_t size() const noexcept { return joints_.size(); }
    bool empty() const noexcept { return joints_.empty(); }

    PoseSpan localPose() noexcept {
        return PoseSpan{std::span<JointPose>(joints_)};
    }
    std::span<const JointPose> localPose() const noexcept {
        return std::span<const JointPose>(joints_);
    }

private:
    std::vector<JointPose> joints_;
};

} // namespace threadmaxx::animation
