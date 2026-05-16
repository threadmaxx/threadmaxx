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
        // §3.10.2 batch 22 — F3 fix. Previously this body allocated a
        // fresh unordered_map + four std::vectors per tick (DDoSing
        // the allocator at 60 Hz for any world with Parent entities).
        // Scratch state is now system-owned and reused across ticks;
        // `clear()` preserves the allocations.
        ctx.single([this, &ctx](Range, CommandBuffer& out) {
            const World& world = ctx.world();
            const auto entities   = world.entities();
            const auto transforms = world.transforms();
            const auto parents    = world.parents();
            const auto masks      = world.componentMasks();
            const auto count = entities.size();
            if (count == 0) return;
            (void)ctx;

            // Handle → dense index map, kept across ticks. Hierarchies
            // span the entire entity set in the worst case so the O(N)
            // map build is unavoidable; subsequent lookups are O(1).
            denseOf_.clear();
            denseOf_.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                denseOf_.emplace(entities[i], i);
            }

            // worldT[i] is the in-progress world transform for dense i.
            // Starts as the stored Transform — correct for roots, will be
            // overwritten for parented entities below. `assign` reuses
            // capacity from previous ticks.
            worldT_.assign(transforms.begin(), transforms.end());
            done_.assign(count, 0);

            // Roots: not Parent-tagged, or Parent handle is invalid or stale.
            for (std::uint32_t i = 0; i < count; ++i) {
                if (!masks[i].has(Component::Parent)) { done_[i] = 1; continue; }
                const auto pH = parents[i].parent;
                if (!pH.valid() || denseOf_.find(pH) == denseOf_.end()) {
                    done_[i] = 1;  // dangling parent → treat as root
                }
            }

            stack_.clear();
            // `onStack[i] != 0` means dense index `i` is currently in
            // `stack` for this start chain. Used to detect cycles
            // (A → B → A) — without this guard, the inner walk would
            // loop forever. Reset on each chain.
            onStack_.assign(count, 0);
            for (std::uint32_t start = 0; start < count; ++start) {
                if (done_[start]) continue;

                // Walk up the chain, pushing entries until we hit a done node.
                stack_.clear();
                std::uint32_t cur = start;
                while (!done_[cur]) {
                    if (onStack_[cur]) {
                        // Cycle: cur is already on the current walk
                        // stack. Treat every entry on the stack as a
                        // dangling root so the next pass leaves them
                        // alone — no infinite loop.
                        for (auto idx : stack_) done_[idx] = 1;
                        break;
                    }
                    onStack_[cur] = 1;
                    stack_.push_back(cur);
                    const auto it = denseOf_.find(parents[cur].parent);
                    // Pre-pass guaranteed validity here, but defend anyway.
                    if (it == denseOf_.end()) {
                        done_[cur] = 1;
                        onStack_[cur] = 0;
                        stack_.pop_back();
                        break;
                    }
                    cur = it->second;
                }
                // Clear the on-stack flags for entries still in stack.
                for (auto idx : stack_) onStack_[idx] = 0;

                // Pop top-down: each child's parent is already done.
                while (!stack_.empty()) {
                    const std::uint32_t child = stack_.back();
                    stack_.pop_back();

                    const std::uint32_t pIdx = denseOf_.find(parents[child].parent)->second;
                    const Transform& parentWorld = worldT_[pIdx];
                    const Transform& local       = parents[child].localOffset;

                    Transform w;
                    w.position    = parentWorld.position
                                  + rotate(parentWorld.orientation, local.position);
                    w.orientation = mul(parentWorld.orientation, local.orientation);
                    if (cfg_.propagateScale) {
                        // Opt-in component-wise scale chain. Useful for
                        // attached props/sockets that should inherit the
                        // parent's scaling factor.
                        w.scale.x = parentWorld.scale.x * local.scale.x;
                        w.scale.y = parentWorld.scale.y * local.scale.y;
                        w.scale.z = parentWorld.scale.z * local.scale.z;
                    } else {
                        w.scale = local.scale;  // not chained — see Parent doc
                    }
                    worldT_[child] = w;
                    done_[child] = 1;

                    if (!transformsEqual(transforms[child], w)) {
                        out.setTransform(entities[child], w);
                    }
                }
            }
        });
    }

private:
    HierarchyConfig cfg_;

    // §3.10.2 batch 22 — per-tick scratch state, reused across ticks
    // so steady-state pays zero allocations after the first tick.
    std::unordered_map<EntityHandle, std::uint32_t> denseOf_;
    std::vector<Transform>                          worldT_;
    std::vector<std::uint8_t>                       done_;
    std::vector<std::uint32_t>                      stack_;
    std::vector<std::uint8_t>                       onStack_;
};

} // namespace

std::unique_ptr<ISystem> makeHierarchySystem(HierarchyConfig cfg) {
    return std::make_unique<HierarchySystem>(cfg);
}

} // namespace threadmaxx
