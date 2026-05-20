/// @file HierarchySystem.cpp
/// Built-in hierarchy propagation system. Reads `Transform + Parent`;
/// writes `Transform`.
///
/// Algorithm: build a handle→dense-index map once per tick, then
/// resolve each parented entity by walking its chain to a root,
/// memoizing computed world transforms along the way. Multi-level
/// chains converge in one pass.
///
/// World pose composition:
///   - position    = parent.position + rotate(parent.orientation, local.position)
///   - orientation = parent.orientation * local.orientation  (Hamilton product)
///   - scale       = local.scale  (NOT chained; see doc/hierarchy.md)
///
/// Runs single-threaded under `ctx.single()` because the DFS read-write
/// pattern is incompatible with parallel iteration (a child reads its
/// own parent's already-resolved world transform).
#include "threadmaxx/System.hpp"
#include "threadmaxx/World.hpp"
#include "threadmaxx/internal/Archetype.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace threadmaxx {

namespace {

// q ⊗ v: rotate vector v by unit quaternion q. Standard reduced form of
// q * (0,v) * q⁻¹.
constexpr Vec3 rotate(const Quat& q, const Vec3& v) noexcept {
    const Vec3 qv{q.x, q.y, q.z};
    const Vec3 t{
        2.0f * (qv.y * v.z - qv.z * v.y),
        2.0f * (qv.z * v.x - qv.x * v.z),
        2.0f * (qv.x * v.y - qv.y * v.x),
    };
    return Vec3{
        v.x + q.w * t.x + (qv.y * t.z - qv.z * t.y),
        v.y + q.w * t.y + (qv.z * t.x - qv.x * t.z),
        v.z + q.w * t.z + (qv.x * t.y - qv.y * t.x),
    };
}

constexpr Quat mul(const Quat& a, const Quat& b) noexcept {
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

bool transformsEqual(const Transform& a, const Transform& b) noexcept {
    return a.position.x    == b.position.x    && a.position.y    == b.position.y    && a.position.z    == b.position.z
        && a.orientation.x == b.orientation.x && a.orientation.y == b.orientation.y && a.orientation.z == b.orientation.z && a.orientation.w == b.orientation.w
        && a.scale.x       == b.scale.x       && a.scale.y       == b.scale.y       && a.scale.z       == b.scale.z;
}

class HierarchySystem : public ISystem {
public:
    explicit HierarchySystem(HierarchyConfig cfg) noexcept : cfg_(cfg) {}

    const char* name() const noexcept override { return "hierarchy"; }

    ComponentSet reads() const noexcept override {
        return ComponentSet{Component::Transform}
             | ComponentSet{Component::Parent};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }

    void update(SystemContext& ctx) override {
        // 2026-05-20 — chunk-iteration rewrite. The previous body
        // walked every alive entity (O(N), via the stitched view) to
        // build a handle→dense-index map. Under heavy-entity loads
        // (60k+ entities, only one of which actually carries Parent
        // — common in any scene with most entities un-parented) the
        // O(N) sweep was the largest single CPU cost in
        // EngineImpl::step. The new path walks only Parent-bearing
        // archetype chunks and resolves each entry through
        // `World::tryGetTransform` on the parent handle. Multi-level
        // chains converge by re-iterating until the parented-set
        // shrinks to zero or stabilises (each pass resolves every
        // entry whose parent is already finalised). Steady-state on
        // single-level hierarchies (the common case) is one pass.
        ctx.single([this, &ctx](Range, CommandBuffer& out) {
            const World& world = ctx.world();
            const auto chunkCount = world.archetypeChunkCount();
            if (chunkCount == 0) return;

            // Collect parented entries from Parent-bearing chunks
            // only. `pending_` carries dense-into-`worldT_` indices
            // so the multi-pass resolver can build chains without
            // re-walking chunks.
            pending_.clear();
            handleToWorldIdx_.clear();
            worldT_.clear();
            for (std::size_t c = 0; c < chunkCount; ++c) {
                const auto& chunk = world.archetypeChunk(c);
                if (!chunk.mask.has(Component::Parent)) continue;
                const auto n = chunk.entities.size();
                for (std::size_t r = 0; r < n; ++r) {
                    Pending p;
                    p.entity      = chunk.entities[r];
                    p.parent      = chunk.parents[r].parent;
                    p.local       = chunk.parents[r].localOffset;
                    p.storedT     = chunk.transforms[r];
                    p.worldIdx    = static_cast<std::uint32_t>(worldT_.size());
                    p.resolved    = false;
                    pending_.push_back(p);
                    worldT_.push_back(p.storedT);
                    handleToWorldIdx_[p.entity] = p.worldIdx;
                }
            }
            if (pending_.empty()) return;

            // Multi-pass resolver. Each pass walks `pending_`; an
            // entry is resolvable iff its parent is NOT in
            // `handleToWorldIdx_` (i.e. the parent is a root, world
            // transform readable from storage) OR the parent IS in
            // the map AND already resolved (its `worldT_` slot is
            // finalised). When a pass finishes with no progress, any
            // remaining entries either have a dangling parent or
            // form a cycle — both are clamped to their stored
            // transform.
            bool progress = true;
            while (progress) {
                progress = false;
                for (auto& p : pending_) {
                    if (p.resolved) continue;
                    Transform parentWorld;
                    if (auto it = handleToWorldIdx_.find(p.parent);
                        it != handleToWorldIdx_.end()) {
                        const auto& other = pending_[
                            indexOfWorldIdx_(it->second)];
                        if (!other.resolved) continue;
                        parentWorld = worldT_[it->second];
                    } else {
                        // Parent is a root (no Parent component) or
                        // dangling. Read from world storage.
                        const Transform* t =
                            world.tryGetTransform(p.parent);
                        if (!t) { p.resolved = true; continue; }
                        parentWorld = *t;
                    }
                    Transform w;
                    w.position = parentWorld.position +
                        rotate(parentWorld.orientation, p.local.position);
                    w.orientation =
                        mul(parentWorld.orientation, p.local.orientation);
                    if (cfg_.propagateScale) {
                        w.scale.x = parentWorld.scale.x * p.local.scale.x;
                        w.scale.y = parentWorld.scale.y * p.local.scale.y;
                        w.scale.z = parentWorld.scale.z * p.local.scale.z;
                    } else {
                        w.scale = p.local.scale;
                    }
                    worldT_[p.worldIdx] = w;
                    p.resolved = true;
                    progress = true;
                }
            }

            // Emit setTransform only when the composed world differs
            // from the chunk's stored Transform. Skips unchanged
            // entries to keep commit churn low.
            for (const auto& p : pending_) {
                if (!p.resolved) continue;
                const Transform& w = worldT_[p.worldIdx];
                if (!transformsEqual(p.storedT, w)) {
                    out.setTransform(p.entity, w);
                }
            }
        });
    }

    // Helper for the resolver. `handleToWorldIdx_` maps to a
    // `worldT_` slot, which is parallel to `pending_`; this gives
    // the corresponding `pending_` index back.
    std::size_t indexOfWorldIdx_(std::uint32_t worldIdx) const noexcept {
        // The two indices coincide because `pending_.push_back` and
        // `worldT_.push_back` are interleaved 1:1 in the collection
        // loop above.
        return worldIdx;
    }

private:
    HierarchyConfig cfg_;

    struct Pending {
        EntityHandle  entity;
        EntityHandle  parent;
        Transform     local;
        Transform     storedT;
        std::uint32_t worldIdx = 0;
        bool          resolved = false;
    };

    // 2026-05-20 — chunk-iteration scratch state, reused across
    // ticks. `pending_` is filled from Parent-bearing chunks only,
    // so worlds dominated by un-parented entities (the common case)
    // pay O(number-of-parented-entities) per tick instead of O(N).
    std::vector<Pending>                              pending_;
    std::vector<Transform>                            worldT_;
    std::unordered_map<EntityHandle, std::uint32_t>   handleToWorldIdx_;
};

} // namespace

std::unique_ptr<ISystem> makeHierarchySystem(HierarchyConfig cfg) {
    return std::make_unique<HierarchySystem>(cfg);
}

} // namespace threadmaxx
