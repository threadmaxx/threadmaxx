/// @file EngineImpl.cpp
/// Heart of the engine. Owns the lifecycle (`initialize` / `step` / `run` /
/// `shutdown`), the wave scheduler, the commit phase, and the double-
/// buffered render-frame publication.
///
/// Maintainer reading order:
///   - `step()` is the canonical tick: reset per-system stats, run waves
///      (each wave fans out across helper threads, with the tail running
///      on the sim thread to avoid a wasted join), commit each system's
///      buffers in registration order, advance tick, build + publish
///      render frame.
///   - `commitBuffer()` is the only path that mutates `EntityStorage`.
///      Every new built-in component or command variant must extend the
///      `std::visit` lambda here.
///   - `rebuildWaves()` is the greedy first-fit packer; it is recomputed
///      every `registerSystem` so wave shape stays consistent with the
///      currently-registered set.
///   - `buildRenderFrame()` is the only path that fills
///      `renderInstanceBuffers_`; publish is via
///      `frontIndex_.store(back, release)`.
#include "EngineImpl.hpp"

#include "threadmaxx/Engine.hpp"
#include "threadmaxx/EventChannel.hpp"
#include "threadmaxx/Game.hpp"
#include "threadmaxx/Renderer.hpp"
#include "threadmaxx/Serialization.hpp"
#include "threadmaxx/SkipPolicy.hpp"
#include "threadmaxx/Telemetry.hpp"
#include "threadmaxx/Trace.hpp"

#include <cstring>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>
#include <utility>

namespace threadmaxx::internal {

namespace {

// Drop-in `std::latch` replacement that is visible to ThreadSanitizer.
//
// libstdc++'s `std::latch::wait()` is implemented on top of
// `std::atomic<int>::wait()` which lowers to a futex syscall. TSAN
// does NOT model the happens-before edge that this syscall
// establishes, so any non-atomic data written by a worker before
// `count_down()` and read by the sim thread after `wait()` is flagged
// as a data race — even though the synchronization is real and the
// code is correct.
//
// This shim wraps the same semantics behind a mutex+CV pair, which
// TSAN models perfectly. Workers `acq_rel` decrement an atomic
// counter; only the final decrementer takes the mutex and notifies.
// The waiter always takes the mutex before checking the count, which
// closes the wait/notify race and gives TSAN the
// mutex-release-acquire edge it needs to see worker writes.
//
// Cost: one atomic decrement per `count_down`, plus one mutex acquire
// on the final decrement and one on the wait. Below `std::latch`'s
// futex syscall cost in the contended case; the spec-grade test
// `commit_soak_test` runs in ~33s under TSAN with this shim, matching
// the pre-shim wall clock.
class JobLatch {
public:
    explicit JobLatch(std::ptrdiff_t n) noexcept
        : count_(n) {}

    JobLatch(const JobLatch&) = delete;
    JobLatch& operator=(const JobLatch&) = delete;

    // Every count_down takes the lock so the count update + notify are
    // serialized with the waiter's predicate check. The earlier
    // optimization (atomic-decrement, notify only on the final
    // decrement) had a missed-wakeup hazard under TSAN: if the final
    // decrementer's atomic decrement and the waiter's predicate check
    // both happened to see `count == 0` before the lock acquisition
    // sequenced them, the waiter could observe `count == 0` outside the
    // mutex but the worker hadn't yet entered the notify region —
    // safe under C++ memory model, but TSAN couldn't see the HB and
    // worker also wasn't guaranteed to make further progress past the
    // notify. The "always lock" form serializes everything through the
    // mutex; the cost is a single uncontended mutex per count_down,
    // which under typical 8-job parallelFor patterns is well below
    // the surrounding parallelism's overhead.
    void count_down() noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        if (--count_ == 0) {
            cv_.notify_all();
        }
    }

    void wait() noexcept {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this] { return count_ == 0; });
    }

private:
    // No longer atomic — every read/write is mutex-protected.
    std::ptrdiff_t          count_;
    mutable std::mutex      mtx_;
    std::condition_variable cv_;
};

// Picks a sensible chunk size when the caller passes grain=0. The
// active system's `preferredGrain()` (batch 11) wins when it's
// non-zero; otherwise aim for roughly 4 chunks per worker so load
// balances without overwhelming the queue with tiny jobs.
std::uint32_t pickGrain(std::uint32_t count, std::uint32_t workers,
                        std::uint32_t preferred) {
    if (count == 0) return 1;
    if (preferred > 0) {
        return std::min(preferred, std::max(1u, count));
    }
    const std::uint32_t target = std::max(1u, workers * 4);
    const std::uint32_t grain  = (count + target - 1) / target;
    return std::max(1u, grain);
}

// FNV-1a-64 byte mixer. Used by `commitBuffer` to maintain
// `commitHashAcc_` — see `EngineStats::commitHash`.
constexpr inline std::uint64_t mixHashByte(std::uint64_t h,
                                           std::uint8_t b) noexcept {
    h ^= b;
    h *= 0x100000001b3ull;
    return h;
}

// Mix the raw byte representation of a trivially-copyable POD. The
// component PODs (Vec3 / Quat / Transform / …) are tightly packed
// floats with no internal padding, so this produces stable output
// across runs and machines.
template <typename T>
inline std::uint64_t mixHashBytes(std::uint64_t h, const T& v) noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "commitHash requires trivially-copyable inputs");
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        h = mixHashByte(h, static_cast<std::uint8_t>(p[i]));
    }
    return h;
}

// §3.6 batch 30 — Hash a chunk's full content into a 64-bit FNV-1a-64
// fingerprint. Used by the end-of-step `finalizeCommitHash` rollup to
// refresh a dirty chunk's `cachedHash`. Inputs are mixed in a fixed,
// architecture-independent order:
//   1. mask.bits()         (8 bytes, distinguishes chunks of different shape)
//   2. row count           (8 bytes)
//   3. entities[]          (8 bytes × count, preserves entity-identity)
//   4. each built-in dense vector, in component-enum order, gated by mask
//   5. each user column,   in registration (ascending-bit) order:
//        col.bit, col.stride, then col.bytes (count rows × stride bytes)
//
// Empty chunks fold down to `mix(basis, mask.bits()) ; mix(_, 0)` —
// the mask bits still distinguish a still-empty archetype from one
// that never existed, since `finalizeCommitHash` walks every chunk.
std::uint64_t hashChunkContent(const ArchetypeChunk& c) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    const std::uint64_t maskBits = c.mask.bits();
    h = mixHashBytes(h, maskBits);
    const std::uint64_t count = c.entities.size();
    h = mixHashBytes(h, count);
    if (count == 0) {
        // No rows; per-vector bytes are empty. Mix the column metadata
        // anyway so a chunk that *carries* a user column with no rows
        // hashes differently from one without that column.
        for (const auto& col : c.userColumns) {
            h = mixHashBytes(h, col.bit);
            h = mixHashBytes(h, col.stride);
        }
        return h;
    }
    // Entity handles ride into the hash so reordering rows within a
    // chunk (e.g. via swap-and-pop) changes the hash. That's the
    // desired contract: the hash reflects observable structure, not
    // just multiset content.
    {
        const auto* p = reinterpret_cast<const std::byte*>(c.entities.data());
        const std::size_t bytes = count * sizeof(EntityHandle);
        for (std::size_t i = 0; i < bytes; ++i) {
            h = mixHashByte(h, static_cast<std::uint8_t>(p[i]));
        }
    }
    auto mixVecBytes = [&h, count](const auto* data, std::size_t stride) {
        const auto* p = reinterpret_cast<const std::byte*>(data);
        const std::size_t bytes = count * stride;
        for (std::size_t i = 0; i < bytes; ++i) {
            h = mixHashByte(h, static_cast<std::uint8_t>(p[i]));
        }
    };
    if (c.mask.has(Component::Transform))
        mixVecBytes(c.transforms.data(), sizeof(Transform));
    if (c.mask.has(Component::Velocity))
        mixVecBytes(c.velocities.data(), sizeof(Velocity));
    if (c.mask.has(Component::RenderTag))
        mixVecBytes(c.renderTags.data(), sizeof(RenderTag));
    if (c.mask.has(Component::UserData))
        mixVecBytes(c.userData.data(), sizeof(UserData));
    if (c.mask.has(Component::Acceleration))
        mixVecBytes(c.accelerations.data(), sizeof(Acceleration));
    if (c.mask.has(Component::Parent))
        mixVecBytes(c.parents.data(), sizeof(Parent));
    if (c.mask.has(Component::Health))
        mixVecBytes(c.healths.data(), sizeof(Health));
    if (c.mask.has(Component::Faction))
        mixVecBytes(c.factions.data(), sizeof(Faction));
    if (c.mask.has(Component::AnimationStateRef))
        mixVecBytes(c.animationStates.data(), sizeof(AnimationStateRef));
    if (c.mask.has(Component::PhysicsBodyRef))
        mixVecBytes(c.physicsBodies.data(), sizeof(PhysicsBodyRef));
    if (c.mask.has(Component::NavAgentRef))
        mixVecBytes(c.navAgents.data(), sizeof(NavAgentRef));
    if (c.mask.has(Component::BoundingVolume))
        mixVecBytes(c.boundingVolumes.data(), sizeof(BoundingVolume));
    for (const auto& col : c.userColumns) {
        h = mixHashBytes(h, col.bit);
        h = mixHashBytes(h, col.stride);
        for (std::byte b : col.bytes) {
            h = mixHashByte(h, static_cast<std::uint8_t>(b));
        }
    }
    return h;
}

// §3.6 batch 13b — Apply ONE command's storage mutation. No hashing.
// Returns the resulting `EntityHandle` for `CmdSpawn` so callers can
// hash it post-apply (the value-vs-reserved spawn paths produce
// distinguishable handles, which the hash must reflect).
// `kInvalidEntity` for all other variants.
EntityHandle applyCommandImpl(detail::Command& cmd,
                              EntityStorage& storage) noexcept {
    EntityHandle resultHandle = kInvalidEntity;
    // §3.9.3 batch 18 — CmdSpawn and CmdAddUserComponent live behind a
    // `std::unique_ptr` to keep the variant size small (256 B → 56 B).
    // The `unwrap` helper transparently dereferences for those two
    // alternatives so the rest of the visit branches use the same
    // field shape as before.
    auto unwrap = [](auto& cv) -> auto& {
        using TT = std::decay_t<decltype(cv)>;
        if constexpr (std::is_same_v<TT, detail::CmdSpawnPtr> ||
                      std::is_same_v<TT, detail::CmdAddUserComponentPtr>) {
            return *cv;
        } else {
            return cv;
        }
    };
    std::visit([&](auto& cv) {
        auto& c = unwrap(cv);
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, detail::CmdSpawn>) {
            if (c.reserved.valid()) {
                // §3.5: spawn into a slot previously taken via
                // SystemContext::reserveHandle(). Falls back to a
                // fresh allocation if the reservation was discarded
                // (e.g. by a competing path that consumed it first).
                if (storage.materializeReserved(c.reserved,
                        c.transform, c.velocity, c.render, c.userData,
                        c.acceleration, c.parent,
                        c.health, c.faction, c.animationState,
                        c.physicsBody, c.navAgent, c.boundingVolume,
                        c.initialMask)) {
                    resultHandle = c.reserved;
                } else {
                    resultHandle = storage.spawn(c.transform, c.velocity,
                                  c.render, c.userData,
                                  c.acceleration, c.parent,
                                  c.health, c.faction, c.animationState,
                                  c.physicsBody, c.navAgent, c.boundingVolume,
                                  c.initialMask);
                }
            } else {
                resultHandle = storage.spawn(c.transform, c.velocity,
                              c.render, c.userData,
                              c.acceleration, c.parent,
                              c.health, c.faction, c.animationState,
                              c.physicsBody, c.navAgent, c.boundingVolume,
                              c.initialMask);
            }
        } else if constexpr (std::is_same_v<T, detail::CmdDestroy>) {
            storage.destroy(c.entity);
        } else if constexpr (std::is_same_v<T, detail::CmdSetTransform>) {
            if (auto* p = storage.mutTransform(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetVelocity>) {
            if (auto* p = storage.mutVelocity(c.entity))  *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetRenderTag>) {
            // Migrate first so the destination chunk has the
            // RenderTag slot, then write the value. The auto-derive
            // matches the legacy "RenderTag iff meshId>=0" rule.
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                if (c.value.meshId >= 0) newMask.add(Component::RenderTag);
                else                     newMask.remove(Component::RenderTag);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
            if (auto* p = storage.mutRenderTag(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetUserData>) {
            if (auto* p = storage.mutUserData(c.entity))  *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetAcceleration>) {
            if (auto* p = storage.mutAcceleration(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetParent>) {
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                if (c.value.parent.valid()) newMask.add(Component::Parent);
                else                        newMask.remove(Component::Parent);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
            if (auto* p = storage.mutParent(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetHealth>) {
            // §3.1 batch-5 set* methods: attaching a value attaches
            // the presence bit. Migrate FIRST so the destination
            // archetype has a Health slot to write into.
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.add(Component::Health);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
            if (auto* p = storage.mutHealth(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetFaction>) {
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.add(Component::Faction);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
            if (auto* p = storage.mutFaction(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetAnimationState>) {
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.add(Component::AnimationStateRef);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
            if (auto* p = storage.mutAnimationStateRef(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetPhysicsBody>) {
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.add(Component::PhysicsBodyRef);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
            if (auto* p = storage.mutPhysicsBodyRef(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetNavAgent>) {
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.add(Component::NavAgentRef);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
            if (auto* p = storage.mutNavAgentRef(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetBoundingVolume>) {
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.add(Component::BoundingVolume);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
            if (auto* p = storage.mutBoundingVolume(c.entity)) *p = c.value;
        } else if constexpr (std::is_same_v<T, detail::CmdSetComponentMask>) {
            storage.setMaskAndMigrate(c.entity, c.value);
        } else if constexpr (std::is_same_v<T, detail::CmdAddTag>) {
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.add(c.tag);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
        } else if constexpr (std::is_same_v<T, detail::CmdRemoveTag>) {
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.remove(c.tag);
                storage.setMaskAndMigrate(c.entity, newMask);
            }
        } else if constexpr (std::is_same_v<T, detail::CmdAddUserComponent>) {
            // §3.1 batch 6b: migrate the entity into the
            // destination archetype (which gets a UserComponentColumn
            // for this bit during getOrCreateArchetype) and write
            // the user-supplied blob into the new row. No-op for
            // stale handles.
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.add(static_cast<Component>(1ull << c.bit));
                storage.setMaskAndMigrate(c.entity, newMask);
                const auto loc = storage.locate(c.entity);
                auto& chunk = storage.archetypes().chunks()[loc.archetype];
                if (auto* col = chunk.findUserColumn(c.bit)) {
                    std::memcpy(col->rowPtr(loc.row), c.data(), col->stride);
                }
            }
        } else if constexpr (std::is_same_v<T, detail::CmdRemoveUserComponent>) {
            // Idempotent: bit absent → migrate is a no-op fast path.
            if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                ComponentSet newMask = *m;
                newMask.remove(static_cast<Component>(1ull << c.bit));
                storage.setMaskAndMigrate(c.entity, newMask);
            }
        }
    }, cmd);
    return resultHandle;
}

// §3.6 batch 13a — Mix one command's full hash contribution into `h`.
// Includes the variant discriminator AND the per-variant payload
// bytes. For `CmdSpawn` the caller supplies the result handle (which
// `applyCommandImpl` returned).
std::uint64_t hashCommandImpl(std::uint64_t h, const detail::Command& cmd,
                              EntityHandle spawnResult) noexcept {
    h = mixHashByte(h, static_cast<std::uint8_t>(cmd.index()));
    // §3.9.3 batch 18 — Dereference the unique_ptr-backed variants so
    // the hash bytes match the pre-batch-18 layout: the inner POD's
    // bytes are mixed, not the wrapper's pointer.
    auto unwrap = [](const auto& cv) -> const auto& {
        using TT = std::decay_t<decltype(cv)>;
        if constexpr (std::is_same_v<TT, detail::CmdSpawnPtr> ||
                      std::is_same_v<TT, detail::CmdAddUserComponentPtr>) {
            return *cv;
        } else {
            return cv;
        }
    };
    std::visit([&](const auto& cv) {
        const auto& c = unwrap(cv);
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, detail::CmdSpawn>) {
            h = mixHashBytes(h, c);
            h = mixHashBytes(h, spawnResult);
        } else if constexpr (std::is_same_v<T, detail::CmdDestroy>) {
            h = mixHashBytes(h, c.entity);
        } else if constexpr (std::is_same_v<T, detail::CmdSetComponentMask>) {
            h = mixHashBytes(h, c.entity);
            const std::uint64_t bits = c.value.bits();
            h = mixHashBytes(h, bits);
        } else if constexpr (std::is_same_v<T, detail::CmdAddTag> ||
                             std::is_same_v<T, detail::CmdRemoveTag>) {
            h = mixHashBytes(h, c.entity);
            const std::uint64_t tag = static_cast<std::uint64_t>(c.tag);
            h = mixHashBytes(h, tag);
        } else if constexpr (std::is_same_v<T, detail::CmdAddUserComponent>) {
            h = mixHashBytes(h, c.entity);
            h = mixHashBytes(h, c.bit);
            const auto* blob = reinterpret_cast<const std::byte*>(c.data());
            for (std::size_t i = 0; i < c.stride; ++i) {
                h = mixHashByte(h, static_cast<std::uint8_t>(blob[i]));
            }
        } else if constexpr (std::is_same_v<T, detail::CmdRemoveUserComponent>) {
            h = mixHashBytes(h, c.entity);
            h = mixHashBytes(h, c.bit);
        } else {
            // All remaining Set* variants share the (entity, value) shape.
            h = mixHashBytes(h, c.entity);
            h = mixHashBytes(h, c.value);
        }
    }, cmd);
    return h;
}

// True iff a command type can change the entity's archetype (any
// mask-toggling op). Used by the sharded commit's pass A to build
// the migrating-entity set. The four value-only setters
// (SetTransform/Velocity/Acceleration/UserData) are the chunk-local
// fast path.
bool commandIsMigrating(const detail::Command& cmd) noexcept {
    return std::visit([](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        return !(std::is_same_v<T, detail::CmdSetTransform> ||
                 std::is_same_v<T, detail::CmdSetVelocity> ||
                 std::is_same_v<T, detail::CmdSetAcceleration> ||
                 std::is_same_v<T, detail::CmdSetUserData>);
    }, cmd);
}

// SHARDED_OPTIMISATION.md S8 — chunk-locator hook installed on each
// CommandBuffer at wave start. Resolves an entity handle to its
// current archetype index (or `kInvalidArchetype` for stale / not-
// yet-spawned handles, which routes the command to the global lane).
//
// Thread-safe under the wave-recording invariant: workers only read
// `slots_`; the sole mutator during a wave is `EntityStorage::
// reserveHandle()` which appends new slots under `reservationMtx_`.
// As long as `slots_` has been pre-reserved enough to avoid
// reallocation, concurrent reads of existing slots are safe.
std::uint32_t commandBufferLocator(const void* ctx,
                                   EntityHandle h) noexcept {
    const auto* storage = static_cast<const EntityStorage*>(ctx);
    const auto loc = storage->locate(h);
    return loc.archetype;
}

// Returns the target entity of a non-spawn command. For `CmdSpawn`
// (which creates a new entity), returns the reserved handle if
// present or `kInvalidEntity` otherwise. The migrating-set tracking
// in pass A only cares about *existing* entities, so the spawn-with-
// reservation case is correctly captured (the reserved handle is
// already live).
EntityHandle commandTargetEntity(const detail::Command& cmd) noexcept {
    return std::visit([](const auto& c) -> EntityHandle {
        using T = std::decay_t<decltype(c)>;
        // §3.9.3 batch 18 — unique_ptr-backed variants need a deref.
        if constexpr (std::is_same_v<T, detail::CmdSpawnPtr>) {
            return c->reserved;
        } else if constexpr (std::is_same_v<T, detail::CmdAddUserComponentPtr>) {
            return c->entity;
        } else if constexpr (std::is_same_v<T, detail::CmdSpawn>) {
            return c.reserved;
        } else {
            return c.entity;
        }
    }, cmd);
}

} // namespace

// ---- SystemContextImpl --------------------------------------------------

// SHARDED_OPTIMISATION.md S8 — installed on each fresh CommandBuffer
// when the engine runs the sharded commit. Skipping it when
// `singleThreadedCommit == true` saves the per-record locator call
// and bucket push on the serial path.
void SystemContextImpl::installLocators(std::size_t firstIdx,
                                        std::size_t count) noexcept {
    const auto& cfg = engine_.config();
    if (cfg.singleThreadedCommit || !cfg.recordTimeRouting) return;
    const auto* storage = &engine_.world().impl_().storage;
    for (std::size_t i = 0; i < count; ++i) {
        buffers_[firstIdx + i].setLocator(&commandBufferLocator, storage);
    }
}

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFn fn) {
    parallelFor(count, grain, std::move(fn), JobPriority::Normal);
}

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFnArena fn) {
    parallelFor(count, grain, std::move(fn), JobPriority::Normal);
}

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFn fn,
                                    JobPriority priority) {
    if (count == 0 || !fn) return;

    const std::uint32_t workers = engine_.jobs().workerCount();
    if (grain == 0) grain = pickGrain(count, workers, preferredGrain_);

    std::uint32_t chunkCount = (count + grain - 1) / grain;
    // ADAPTIVE_TUNING.md T2 — clamp sub-job count to the system's
    // declared worker cap. Acts after the grain heuristic so it
    // overrides both `pickGrain` and explicit caller-supplied grain
    // (the cap is the system's authoritative scheduling hint).
    if (preferredWorkerCap_ > 0 && chunkCount > preferredWorkerCap_) {
        chunkCount = preferredWorkerCap_;
        grain      = (count + chunkCount - 1) / chunkCount;
    }

    // Reserve command buffers up front so emplace_back does not invalidate
    // pointers while jobs are running. arenas_ grows in lockstep; the
    // legacy JobFn variant leaves each entry default-constructed (no
    // allocation paid).
    const std::size_t firstIdx = buffers_.size();
    buffers_.resize(firstIdx + chunkCount);
    arenas_.resize(firstIdx + chunkCount);
    installLocators(firstIdx, chunkCount);

    JobLatch done(chunkCount);
    for (std::uint32_t c = 0; c < chunkCount; ++c) {
        const std::uint32_t begin = c * grain;
        const std::uint32_t end   = std::min(begin + grain, count);
        CommandBuffer* cb = &buffers_[firstIdx + c];
        auto userFn = fn;
        // ADAPTIVE_TUNING.md T3 — time the user lambda and fold the
        // nanosecond count into the context's accumulator. The clock
        // pair brackets only the user code; latch bookkeeping is not
        // charged to the sub-job's reported duration.
        engine_.jobs().submit([userFn, begin, end, cb, &done, this] {
            const auto t0 = std::chrono::steady_clock::now();
            userFn(Range{begin, end}, *cb);
            const auto ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - t0).count());
            subJobNanos_.fetch_add(ns, std::memory_order_relaxed);
            done.count_down();
        }, priority);
    }
    jobsSubmitted_ += chunkCount;
    // Sample queue depth right after submit — captures congestion the
    // system actually saw. Reading the atomic is cheap.
    const std::uint32_t depth = engine_.jobs().outstanding();
    if (depth > peakQueueDepth_) peakQueueDepth_ = depth;
    const auto waitStart = std::chrono::steady_clock::now();
    done.wait();
    waitSeconds_ += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - waitStart).count();
}

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFnArena fn,
                                    JobPriority priority) {
    if (count == 0 || !fn) return;

    const std::uint32_t workers = engine_.jobs().workerCount();
    if (grain == 0) grain = pickGrain(count, workers, preferredGrain_);

    std::uint32_t chunkCount = (count + grain - 1) / grain;
    // ADAPTIVE_TUNING.md T2 — sub-job cap; see the JobFn overload.
    if (preferredWorkerCap_ > 0 && chunkCount > preferredWorkerCap_) {
        chunkCount = preferredWorkerCap_;
        grain      = (count + chunkCount - 1) / chunkCount;
    }

    const std::size_t firstIdx = buffers_.size();
    buffers_.resize(firstIdx + chunkCount);
    arenas_.resize(firstIdx + chunkCount);
    installLocators(firstIdx, chunkCount);

    JobLatch done(chunkCount);
    for (std::uint32_t c = 0; c < chunkCount; ++c) {
        const std::uint32_t begin = c * grain;
        const std::uint32_t end   = std::min(begin + grain, count);
        CommandBuffer*  cb    = &buffers_[firstIdx + c];
        ScratchArena*   arena = &arenas_[firstIdx + c];
        auto userFn = fn;
        // ADAPTIVE_TUNING.md T3 — see the JobFn overload.
        engine_.jobs().submit([userFn, begin, end, cb, arena, &done, this] {
            const auto t0 = std::chrono::steady_clock::now();
            userFn(Range{begin, end}, *cb, *arena);
            const auto ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - t0).count());
            subJobNanos_.fetch_add(ns, std::memory_order_relaxed);
            done.count_down();
        }, priority);
    }
    jobsSubmitted_ += chunkCount;
    const std::uint32_t depth = engine_.jobs().outstanding();
    if (depth > peakQueueDepth_) peakQueueDepth_ = depth;
    const auto waitStart = std::chrono::steady_clock::now();
    done.wait();
    waitSeconds_ += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - waitStart).count();
}

void SystemContextImpl::single(JobFn fn) {
    if (!fn) return;
    const std::size_t firstIdx = buffers_.size();
    buffers_.emplace_back();
    arenas_.emplace_back();
    installLocators(firstIdx, 1);
    fn(Range{0, 0}, buffers_.back());
}

void SystemContextImpl::single(JobFnArena fn) {
    if (!fn) return;
    const std::size_t firstIdx = buffers_.size();
    buffers_.emplace_back();
    arenas_.emplace_back();
    installLocators(firstIdx, 1);
    fn(Range{0, 0}, buffers_.back(), arenas_.back());
}

EntityHandle SystemContextImpl::reserveHandle() {
    return engine_.world().impl_().storage.reserveHandle();
}

std::uint32_t SystemContextImpl::reserveHandles(std::uint32_t count,
                                                std::span<EntityHandle> out) {
    const std::uint32_t n = std::min(count,
        static_cast<std::uint32_t>(out.size()));
    engine_.world().impl_().storage.reserveHandles(n, out);
    return n;
}

bool SystemContextImpl::shouldYield() const noexcept {
    return engine_.overBudget();
}

// §3.10.4 batch 28 — Worker pool width. Cheap pass-through; the engine
// caches it in JobSystem at construction time.
std::uint32_t SystemContextImpl::workerCount() const noexcept {
    return engine_.workerCount();
}

// ---- EngineImpl ---------------------------------------------------------

namespace {
// §3.10.3 batch 24 (F8) — monotonically increasing per-process counter
// stamped onto each Engine. Used as the validity key for the
// `thread_local` channel cache in `Engine::events<T>()`.
std::atomic<std::uint64_t> sNextEngineSerial{1};
} // namespace

EngineImpl::EngineImpl(const Config& cfg) : cfg_(cfg) {
    jobs_ = std::make_unique<JobSystem>(cfg_.workerCount);
    engineSerial_ = sNextEngineSerial.fetch_add(1, std::memory_order_relaxed);
}

EngineImpl::~EngineImpl() {
    shutdown();
    stopWatchdog_();
    stopSnapshotWorker_();
    // §3.3 channels outlive shutdown so postStep hooks can still pump
    // events. They're owned in raw form (factory/deleter pair); release
    // here, after the worker pool has been torn down.
    for (auto& [type, entry] : eventChannels_) {
        if (entry.deleter && entry.ptr) entry.deleter(entry.ptr);
    }
}

// §3.6.5 batch 15a — IRenderer::onResize forwarding. Stays on whichever
// thread calls it; the documented convention is sim-thread invocation
// matching submitFrame.
void EngineImpl::notifyResize(std::uint32_t width,
                              std::uint32_t height) noexcept {
    if (renderer_) renderer_->onResize(width, height);
}

// §3.6.5 batch 15a — Public Engine::workerCount accessor; resolves from
// the JobSystem (which itself is the source of truth: a Config of 0
// gets mapped to `max(1, hardware_concurrency - 1)` at JobSystem
// construction time).
std::uint32_t EngineImpl::workerCount() const noexcept {
    return jobs_ ? jobs_->workerCount() : 0u;
}

// §3.7 batch 14 — stall watchdog plumbing.

void EngineImpl::setStallTimeout(double seconds) noexcept {
    if (seconds < 0.0) seconds = 0.0;
    stallTimeoutSeconds_.store(seconds, std::memory_order_relaxed);
    if (seconds > 0.0) {
        startWatchdog_();
    } else {
        stopWatchdog_();
    }
}

void EngineImpl::startWatchdog_() {
    if (watchdog_.joinable()) return;  // already running
    watchdogRun_.store(true, std::memory_order_release);
    watchdog_ = std::thread(&EngineImpl::watchdogThreadFn_, this);
}

void EngineImpl::stopWatchdog_() {
    watchdogRun_.store(false, std::memory_order_release);
    if (watchdog_.joinable()) watchdog_.join();
}

void EngineImpl::watchdogThreadFn_() {
    using clock = std::chrono::steady_clock;
    while (watchdogRun_.load(std::memory_order_acquire)) {
        const double timeout =
            stallTimeoutSeconds_.load(std::memory_order_relaxed);
        if (timeout <= 0.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        // Sleep at most quarter-period; ensures we detect within 25% of
        // the configured timeout.
        const auto pollPeriod = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(timeout * 0.25));
        std::this_thread::sleep_for(std::max(std::chrono::milliseconds(10), pollPeriod));

        if (!watchdogRun_.load(std::memory_order_acquire)) break;

        const std::uint64_t startNs =
            watchdogStepStartNs_.load(std::memory_order_relaxed);
        if (startNs == 0) continue;  // step() has not been called yet

        const auto nowNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()).count());
        if (nowNs <= startNs) continue;
        const double elapsedSec = static_cast<double>(nowNs - startNs) / 1e9;
        if (elapsedSec < timeout) continue;

        // Already announced this tick? Skip until the sim thread clears
        // the latch in `step()` (after step finishes).
        bool expected = false;
        if (!watchdogStallEmitted_.compare_exchange_strong(
                expected, true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            continue;
        }

        // Emit the event on the engine's typed channel. The lock-free
        // MPSC channel (§3.6 batch 13c) lets us emit from this thread
        // safely. The event drains on the sim thread at the next
        // tick boundary.
        if (publicEngine_) {
            const std::uint64_t tick = watchdogActiveTick_.load(std::memory_order_relaxed);
            publicEngine_->events<EngineStall>().emit(EngineStall{
                tick, elapsedSec
            });
        }
    }
}

// §3.9.5 batch 20 — async snapshot writer thread. Lazily spawned by
// the first `snapshotAsync` call; joined in `shutdown`.

void EngineImpl::startSnapshotWorker_() {
    if (snapshotWorker_.joinable()) return;
    snapshotStop_.store(false, std::memory_order_release);
    snapshotWorker_ = std::thread([this] {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(snapshotMtx_);
                snapshotCv_.wait(lk, [&] {
                    return snapshotStop_.load(std::memory_order_acquire) ||
                           !snapshotQueue_.empty();
                });
                if (snapshotQueue_.empty()) {
                    if (snapshotStop_.load(std::memory_order_acquire)) return;
                    continue;
                }
                job = std::move(snapshotQueue_.front());
                snapshotQueue_.pop_front();
            }
            // Outside the lock — the user's callback can take however
            // long it wants without blocking the producer (sim thread).
            if (job) job();
        }
    });
}

void EngineImpl::stopSnapshotWorker_() {
    if (!snapshotWorker_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(snapshotMtx_);
        snapshotStop_.store(true, std::memory_order_release);
    }
    snapshotCv_.notify_all();
    snapshotWorker_.join();
    // Drain any remaining queued callbacks synchronously on the
    // calling thread. This is conservative — most callers call
    // `shutdown()` from the sim thread, so the callbacks land there.
    while (!snapshotQueue_.empty()) {
        auto job = std::move(snapshotQueue_.front());
        snapshotQueue_.pop_front();
        if (job) job();
    }
}

void EngineImpl::snapshotAsync(std::function<void(WorldSnapshot)> callback) {
    if (!callback) return;
    // Sim-thread side: capture the snapshot synchronously. The dense
    // arrays are vector copies — fast even at 100k entities — and
    // safe because no commits run concurrently with snapshotAsync
    // (the caller is on the sim thread, between steps or inside a
    // pre/post-step hook).
    WorldSnapshot snap = world_.snapshot();

    if (!snapshotWorker_.joinable()) startSnapshotWorker_();

    {
        std::lock_guard<std::mutex> lk(snapshotMtx_);
        snapshotQueue_.emplace_back(
            [s = std::move(snap), cb = std::move(callback)]() mutable {
                cb(std::move(s));
            });
    }
    snapshotCv_.notify_one();
}

bool EngineImpl::initialize(IGame& game, Engine& publicEngine) {
    if (initialized_) return true;
    game_ = &game;
    publicEngine_ = &publicEngine;

    {
        std::ostringstream os;
        os << "engine initialize: " << jobs_->workerCount()
           << " worker(s), fixedStep=" << cfg_.fixedStepSeconds << "s";
        logger().log(LogLevel::Info, os.str());
    }

    // Recreate the world with the configured capacity. The default-constructed
    // World used a hard-coded 1024; replace it so the user's config wins.
    world_ = World{};
    if (cfg_.initialEntityCapacity > 0) {
        world_.impl_().storage.reserve(cfg_.initialEntityCapacity);
    }
    // §3.1 batch 6b: hand the archetype table a pointer to the
    // engine-owned user-component registry. New chunks consult it when
    // materializing per-bit user columns.
    world_.impl_().storage.archetypes().setUserComponentRegistry(&userRegistry_);

    // Let the game register systems / renderer and seed entities.
    CommandBuffer seed;
    game.onSetup(publicEngine, world_, seed);
    commitBuffer(seed);

    if (renderer_ && !renderer_->initialize()) {
        logger().log(LogLevel::Error, "renderer initialize() returned false");
        return false;
    }

    // Build an initial render frame so the renderer can display state at
    // t=0 before any tick has run.
    buildRenderFrame();
    if (renderer_) {
        const unsigned front = frontIndex_.load(std::memory_order_acquire);
        renderer_->submitFrame(renderFrames_[front]);
    }

    initialized_ = true;
    lastIterationTime_ = std::chrono::steady_clock::now();
    return true;
}

void EngineImpl::registerSystem(std::unique_ptr<ISystem> system) {
    if (!system) return;
    system->onRegister(world_);
    SystemStats ss;
    ss.name = system->name();
    systemStats_.push_back(ss);
    const char* sysName = system->name();
    systemPreferredGrain_.push_back(system->preferredGrain());
    systemPreferredWorkerCap_.push_back(system->preferredWorkerCap());
    systems_.push_back(std::move(system));
    systemRenderBuilders_.emplace_back();
    rebuildWaves();
    std::ostringstream os;
    os << "registered system '" << (sysName ? sysName : "(unnamed)")
       << "' (now " << systems_.size() << " total, "
       << waves_.size() << " wave(s))";
    logger().log(LogLevel::Info, os.str());
}

IResourceLoader* EngineImpl::addResourceLoader(
        std::unique_ptr<IResourceLoader> loader) {
    if (!loader) return nullptr;
    IResourceLoader* raw = loader.get();
    resourceLoaders_.push_back(std::move(loader));
    return raw;
}

LoaderStats EngineImpl::aggregateLoaderStats() const noexcept {
    LoaderStats agg;
    for (const auto& loader : resourceLoaders_) {
        if (!loader) continue;
        const LoaderStats s = loader->stats();
        agg.pendingLoads    += s.pendingLoads;
        agg.inFlight        += s.inFlight;
        agg.ready           += s.ready;
        agg.failed          += s.failed;
        agg.cancelled       += s.cancelled;
        agg.memoryFootprint += s.memoryFootprint;
        agg.memoryBudget    += s.memoryBudget;
    }
    return agg;
}

void EngineImpl::markResourceStale(std::uint32_t index,
                                   std::uint32_t generation,
                                   std::type_index type) {
    // Loaders are filtered by their own implementation — the one that
    // recognizes the type handles the call, the rest no-op via the
    // default virtual.
    for (auto& loader : resourceLoaders_) {
        if (loader) loader->markStale(index, generation, type);
    }
}

bool EngineImpl::preloadUntil(std::function<bool()> done,
                              std::chrono::milliseconds timeout) {
    if (!done) return false;
    if (!publicEngine_) return false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (done()) return true;
        for (auto& loader : resourceLoaders_) {
            if (loader) loader->update(*publicEngine_);
        }
        // Check again after pumping so a loader that finishes in this
        // pass exits the loop immediately.
        if (done()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
}

std::size_t EngineImpl::registerSystemAt(std::size_t position,
                                         std::unique_ptr<ISystem> system) {
    if (!system) return systems_.size();
    if (position > systems_.size()) position = systems_.size();
    system->onRegister(world_);
    SystemStats ss;
    ss.name = system->name();
    systemStats_.insert(systemStats_.begin() +
                        static_cast<std::ptrdiff_t>(position), ss);
    systemPreferredGrain_.insert(systemPreferredGrain_.begin() +
                                 static_cast<std::ptrdiff_t>(position),
                                 system->preferredGrain());
    systemPreferredWorkerCap_.insert(systemPreferredWorkerCap_.begin() +
                                     static_cast<std::ptrdiff_t>(position),
                                     system->preferredWorkerCap());
    systems_.insert(systems_.begin() +
                    static_cast<std::ptrdiff_t>(position),
                    std::move(system));
    systemRenderBuilders_.emplace(systemRenderBuilders_.begin() +
                                  static_cast<std::ptrdiff_t>(position));
    rebuildWaves();
    return position;
}

void EngineImpl::applyPendingTuningPatch() {
    if (!pendingPatch_.has_value()) return;
    const TuningPatch patch = std::move(*pendingPatch_);
    pendingPatch_.reset();
    for (const auto& ov : patch.grainOverrides) {
        // Locate by name. Linear scan is fine — patch sizes are O(1-ish)
        // per tick under any sensible policy.
        std::size_t matchIdx = systems_.size();
        for (std::size_t i = 0; i < systems_.size(); ++i) {
            const char* nm = systems_[i] ? systems_[i]->name() : nullptr;
            if (nm && ov.systemName == nm) { matchIdx = i; break; }
        }
        if (matchIdx == systems_.size()) {
            std::ostringstream os;
            os << "[tuning] grain override for unknown system '"
               << ov.systemName << "' ignored";
            logger().log(LogLevel::Warn, os.str());
            continue;
        }
        const std::uint32_t prev = systemPreferredGrain_[matchIdx];
        systemPreferredGrain_[matchIdx] = ov.preferredGrain;
        std::ostringstream os;
        os << "[tuning] '" << ov.systemName
           << "' preferredGrain " << prev << " -> " << ov.preferredGrain;
        logger().log(LogLevel::Info, os.str());
    }
}

void EngineImpl::rebuildWaves() {
    // §3.4 batch 11: DAG-aware first-fit packer.
    //
    // Edge sources:
    //   1. Read/write conflict (existing behavior): two systems with
    //      overlapping read/write sets must land in different waves.
    //      Modelled as a directed edge from the lower-indexed system to
    //      the higher-indexed one, so the topological tiebreaker matches
    //      registration order in the no-tag case.
    //   2. Tag dependency (new): for each tag a system @c j provides
    //      that some other system @c i lists in `dependencies()`, an
    //      edge `j -> i` (j runs before i). Both directions are
    //      considered — a later-registered system can be the provider
    //      and an earlier-registered one the consumer; the topo sort
    //      moves the consumer to a later wave when needed.
    //
    // Cycle handling: Kahn's algorithm processes systems in index order
    // when in-degrees tie. If a cycle blocks progress, the first stuck
    // system has its tag-only incoming edges dropped (read/write edges
    // are preserved); a warning is logged via @ref ILogger.
    waves_.clear();
    const std::size_t N = systems_.size();
    systemWave_.assign(N, 0);
    systemDependsOn_.assign(N, {});
    if (N == 0) return;

    auto rwConflict = [this](std::size_t a, std::size_t b) -> bool {
        const auto rA = systems_[a]->reads();
        const auto wA = systems_[a]->writes();
        const auto rB = systems_[b]->reads();
        const auto wB = systems_[b]->writes();
        return wA.intersects(wB) || wA.intersects(rB) || rA.intersects(wB);
    };

    auto tagEdge = [this](std::size_t from, std::size_t to) -> bool {
        const auto provides = systems_[from]->provides();
        const auto deps     = systems_[to]->dependencies();
        for (const auto& p : provides) {
            if (!p.valid()) continue;
            for (const auto& d : deps) {
                if (p == d) return true;
            }
        }
        return false;
    };

    std::vector<std::vector<std::size_t>> predecessors(N);
    std::vector<std::vector<std::size_t>> successors(N);
    std::vector<bool> edgeIsTagOnly(0);
    auto addEdge = [&](std::size_t from, std::size_t to, bool tagOnly) {
        predecessors[to].push_back(from);
        successors[from].push_back(to);
        // Track whether the edge is tag-only (so the cycle breaker can
        // distinguish droppable from non-droppable edges).
        edgeIsTagOnly.push_back(tagOnly);
    };
    // Build edges. The (a, b) pair with a<b is iterated once; rw-conflict
    // produces a->b, tag-deps in either direction are checked.
    for (std::size_t a = 0; a < N; ++a) {
        for (std::size_t b = 0; b < N; ++b) {
            if (a == b) continue;
            const bool rw = (a < b) && rwConflict(a, b);
            const bool tag = tagEdge(a, b);
            if (rw || tag) {
                // If both rw and tag, the edge is NOT tag-only.
                addEdge(a, b, /*tagOnly=*/!rw && tag);
            }
        }
    }

    std::vector<std::size_t> inDegree(N, 0);
    for (std::size_t i = 0; i < N; ++i) inDegree[i] = predecessors[i].size();

    std::vector<std::size_t> processed;
    processed.reserve(N);
    std::vector<bool> done(N, false);

    while (processed.size() < N) {
        std::size_t pick = N;
        for (std::size_t i = 0; i < N; ++i) {
            if (!done[i] && inDegree[i] == 0) { pick = i; break; }
        }
        if (pick == N) {
            // Cycle. Find the lowest-indexed undone system and drop its
            // tag-only incoming edges. Read/write edges are preserved —
            // they're load-bearing for memory safety, and rw-only edges
            // are i<j by construction so they cannot form a cycle.
            std::size_t stuck = N;
            for (std::size_t i = 0; i < N; ++i) {
                if (!done[i]) { stuck = i; break; }
            }
            if (stuck == N) break;  // shouldn't happen, but defensive
            std::ostringstream os;
            os << "task graph cycle involving system '"
               << systems_[stuck]->name()
               << "'; dropping its tag-only incoming dependency edges to recover";
            logger().log(LogLevel::Warn, os.str());
            auto& preds = predecessors[stuck];
            std::vector<std::size_t> kept;
            kept.reserve(preds.size());
            for (std::size_t pred : preds) {
                if (pred < stuck && rwConflict(pred, stuck)) {
                    kept.push_back(pred);  // rw-conflict; cannot drop
                } else {
                    // Drop: remove `stuck` from `pred`'s successors list too.
                    auto& s = successors[pred];
                    s.erase(std::remove(s.begin(), s.end(), stuck), s.end());
                }
            }
            preds = std::move(kept);
            inDegree[stuck] = preds.size();
            continue;
        }
        done[pick] = true;
        processed.push_back(pick);
        for (std::size_t succ : successors[pick]) {
            if (inDegree[succ] > 0) inDegree[succ]--;
        }
    }

    // Pack into waves following the topological order.
    for (std::size_t i : processed) {
        std::size_t minWave = 0;
        for (std::size_t pred : predecessors[i]) {
            minWave = std::max(minWave, systemWave_[pred] + 1);
        }
        std::size_t w = minWave;
        while (true) {
            if (w >= waves_.size()) { waves_.emplace_back(); break; }
            bool conflicts = false;
            for (std::size_t j : waves_[w]) {
                if (rwConflict(i, j)) { conflicts = true; break; }
            }
            if (!conflicts) break;
            ++w;
        }
        waves_[w].push_back(i);
        systemWave_[i]      = w;
        systemDependsOn_[i] = predecessors[i];
    }
    (void)edgeIsTagOnly;
}

// §3.9.4 batch 19 — peek at the entity's current archetype mask. Used
// by the pre-reservation heuristic in commitBuffer; returns the empty
// ComponentSet for stale handles so the caller's predicted dst
// silently mismatches and the reserve hint is skipped.
ComponentSet currentMaskOf(EntityStorage& storage, EntityHandle h) noexcept {
    if (!storage.alive(h)) return {};
    const auto loc = storage.locate(h);
    if (loc.archetype >= storage.archetypes().chunks().size()) return {};
    return storage.archetypes().chunks()[loc.archetype].mask;
}

void EngineImpl::commitBuffer(CommandBuffer& cb) {
    auto& storage = world_.impl_().storage;
    auto& cmds = cb.commands();
    commandsThisStep_ += cmds.size();

    // §3.9.4 batch 19 — adjacent migration pre-reservation. When a run
    // of consecutive commands toggles the same mask bit, we predict
    // the destination archetype from the first entity's current mask
    // and pre-reserve enough rows in the destination chunk to absorb
    // the entire run without triggering geometric `vector::push_back`
    // growth on every component vector. Wrong predictions are safe —
    // `reserveChunkRows` is a capacity hint, never a content change.
    // The commit hash is computed in submission order, unchanged.
    //
    // The detection only handles CmdAddTag, CmdRemoveTag, and the four
    // most common §3.1 batch-5 setters that always attach a presence
    // bit (CmdSetHealth, CmdSetFaction, CmdSetBoundingVolume,
    // CmdSetAnimationState). Other migration commands fall through to
    // the per-cmd path.
    constexpr std::size_t kRunThreshold = 8;

    auto tryReservePeek = [&](std::size_t startIdx, std::size_t runLen,
                              auto predictDst) {
        if (runLen < kRunThreshold) return;
        // Predict from the first entity's current archetype.
        const auto& first = cmds[startIdx];
        const auto e = commandTargetEntity(first);
        const ComponentSet srcMask = currentMaskOf(storage, e);
        if (srcMask == ComponentSet{}) return;  // stale handle
        const ComponentSet dstMask = predictDst(srcMask);
        if (dstMask == srcMask) return;          // not actually migrating
        storage.archetypes().reserveChunkRows(dstMask, runLen);
    };

    // SHARDED_OPTIMISATION.md S6 — Batch-migrate path. When a detected
    // run is long enough AND every entity in the run shares the same
    // source archetype, dispatch the migrations through one
    // `setMaskAndMigrateBatch` call instead of N `setMaskAndMigrate`s.
    // The post-migrate value-write (for the Set* variants) still loops
    // per-command. Returns true iff the batch path took over the run.
    const std::size_t kBatchMigrateThreshold = cfg_.batchMigrateThreshold;
    auto tryBatchMigrate = [&](std::size_t startIdx, std::size_t runLen,
                               auto predictDst, auto applyValue) -> bool {
        if (runLen < kBatchMigrateThreshold) return false;
        const auto& chunks = storage.archetypes().chunks();
        const auto firstE = commandTargetEntity(cmds[startIdx]);
        if (!firstE.valid()) return false;
        const auto firstLoc = storage.locate(firstE);
        if (firstLoc.archetype >= chunks.size()) return false;
        const ComponentSet srcMask = chunks[firstLoc.archetype].mask;
        const ComponentSet dstMask = predictDst(srcMask);
        if (dstMask == srcMask) return false;  // bit already in desired state

        batchHandlesScratch_.clear();
        batchHandlesScratch_.reserve(runLen);
        for (std::size_t j = startIdx; j < startIdx + runLen; ++j) {
            const auto e = commandTargetEntity(cmds[j]);
            if (!e.valid()) return false;
            const auto loc = storage.locate(e);
            if (loc.archetype != firstLoc.archetype) return false;
            batchHandlesScratch_.push_back(e);
        }
        if (!storage.setMaskAndMigrateBatch(batchHandlesScratch_, dstMask)) {
            return false;
        }
        const bool legacyHash = cfg_.legacyCommitHash;
        for (std::size_t j = 0; j < runLen; ++j) {
            applyValue(cmds[startIdx + j], batchHandlesScratch_[j]);
            if (legacyHash) {
                commitHashAcc_ = hashCommandImpl(commitHashAcc_,
                    cmds[startIdx + j], kInvalidEntity);
            }
        }
        commitBreakdown_.batchedMigrations += runLen;
        return true;
    };

    std::size_t i = 0;
    while (i < cmds.size()) {
        std::size_t runEnd = i + 1;
        const auto& head = cmds[i];
        bool batched = false;

        if (auto* addP = std::get_if<detail::CmdAddTag>(&head)) {
            const Component tag = addP->tag;
            while (runEnd < cmds.size()) {
                auto* q = std::get_if<detail::CmdAddTag>(&cmds[runEnd]);
                if (!q || q->tag != tag) break;
                ++runEnd;
            }
            tryReservePeek(i, runEnd - i, [tag](ComponentSet src) {
                ComponentSet d = src; d.add(tag); return d;
            });
            batched = tryBatchMigrate(i, runEnd - i,
                [tag](ComponentSet src) {
                    ComponentSet d = src; d.add(tag); return d;
                },
                [](detail::Command&, EntityHandle) {});
        } else if (auto* remP = std::get_if<detail::CmdRemoveTag>(&head)) {
            const Component tag = remP->tag;
            while (runEnd < cmds.size()) {
                auto* q = std::get_if<detail::CmdRemoveTag>(&cmds[runEnd]);
                if (!q || q->tag != tag) break;
                ++runEnd;
            }
            tryReservePeek(i, runEnd - i, [tag](ComponentSet src) {
                ComponentSet d = src; d.remove(tag); return d;
            });
            batched = tryBatchMigrate(i, runEnd - i,
                [tag](ComponentSet src) {
                    ComponentSet d = src; d.remove(tag); return d;
                },
                [](detail::Command&, EntityHandle) {});
        } else if (std::holds_alternative<detail::CmdSetHealth>(head)) {
            while (runEnd < cmds.size() &&
                   std::holds_alternative<detail::CmdSetHealth>(cmds[runEnd])) {
                ++runEnd;
            }
            tryReservePeek(i, runEnd - i, [](ComponentSet src) {
                ComponentSet d = src; d.add(Component::Health); return d;
            });
            batched = tryBatchMigrate(i, runEnd - i,
                [](ComponentSet src) {
                    ComponentSet d = src; d.add(Component::Health); return d;
                },
                [&storage](detail::Command& c, EntityHandle e) {
                    if (auto* p = storage.mutHealth(e)) {
                        *p = std::get<detail::CmdSetHealth>(c).value;
                    }
                });
        } else if (std::holds_alternative<detail::CmdSetFaction>(head)) {
            while (runEnd < cmds.size() &&
                   std::holds_alternative<detail::CmdSetFaction>(cmds[runEnd])) {
                ++runEnd;
            }
            tryReservePeek(i, runEnd - i, [](ComponentSet src) {
                ComponentSet d = src; d.add(Component::Faction); return d;
            });
            batched = tryBatchMigrate(i, runEnd - i,
                [](ComponentSet src) {
                    ComponentSet d = src; d.add(Component::Faction); return d;
                },
                [&storage](detail::Command& c, EntityHandle e) {
                    if (auto* p = storage.mutFaction(e)) {
                        *p = std::get<detail::CmdSetFaction>(c).value;
                    }
                });
        } else if (std::holds_alternative<detail::CmdSetBoundingVolume>(head)) {
            while (runEnd < cmds.size() &&
                   std::holds_alternative<detail::CmdSetBoundingVolume>(cmds[runEnd])) {
                ++runEnd;
            }
            tryReservePeek(i, runEnd - i, [](ComponentSet src) {
                ComponentSet d = src; d.add(Component::BoundingVolume); return d;
            });
            batched = tryBatchMigrate(i, runEnd - i,
                [](ComponentSet src) {
                    ComponentSet d = src; d.add(Component::BoundingVolume); return d;
                },
                [&storage](detail::Command& c, EntityHandle e) {
                    if (auto* p = storage.mutBoundingVolume(e)) {
                        *p = std::get<detail::CmdSetBoundingVolume>(c).value;
                    }
                });
        }
        // else: single-cmd path — fall through with runEnd == i + 1.

        if (!batched) {
            // Apply each command in submission order. Under the v1.x
            // legacy hash path (`Config::legacyCommitHash = true`) the
            // per-tick hash matches the pre-batch-19 reference byte-
            // for-byte. Under the new batch-30 path the per-command mix
            // is skipped — the hash is reconstructed at end of step
            // from per-chunk state (see `finalizeCommitHash`).
            const bool legacyHash = cfg_.legacyCommitHash;
            for (std::size_t j = i; j < runEnd; ++j) {
                const EntityHandle spawnResult = applyCommandImpl(cmds[j], storage);
                if (legacyHash) {
                    commitHashAcc_ = hashCommandImpl(commitHashAcc_, cmds[j], spawnResult);
                }
            }
        }
        i = runEnd;
    }
    cb.clear();
}

EntityHandle EngineImpl::applyCommandNoHash(detail::Command& cmd) {
    return applyCommandImpl(cmd, world_.impl_().storage);
}

void EngineImpl::commitBuffersSharded(std::vector<CommandBuffer>& buffers) {
    if (buffers.empty()) return;
    auto& storage = world_.impl_().storage;

    // SHARDED_OPTIMISATION.md S0 — pass-breakdown timer. Always-on,
    // ~5 `steady_clock::now()` calls per call (~30–60 ns total).
    using bdClock = std::chrono::steady_clock;
    const auto bdT0 = bdClock::now();
    auto bdElapsedNs = [](bdClock::time_point a, bdClock::time_point b) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
    };

    // Count totals using each buffer's `valueOnlyCount` tally so we
    // can take an early decision without scanning the variant stream.
    std::size_t totalCommands = 0;
    std::size_t totalValueOnly = 0;
    for (const auto& cb : buffers) {
        totalCommands  += cb.commands().size();
        totalValueOnly += cb.valueOnlyCount();
    }
    if (totalCommands == 0) return;
    commandsThisStep_ += totalCommands;
    commitBreakdown_.totalCommands  += totalCommands;
    commitBreakdown_.totalValueOnly += totalValueOnly;

    // §3.9.6 batch 21 — three pre-conditions for the sharded path to
    // be worth its classifier overhead. Any one failing falls through
    // to the single-threaded commit (same correctness, less overhead).
    //
    // (1) `totalCommands < kShardedMinCommands` — small batches can't
    //     amortize the two-pass overhead.
    // (2) `totalValueOnly == 0` — every command migrates, so Pass C
    //     would be empty. Doing Pass A + Pass B for nothing.
    // (3) `chunks().size() < 2` — only one archetype exists, so even
    //     the value-only commands would all bin into one job; no
    //     parallelism possible.
    //
    // The migration-heavy workload (`addRemoveTag` in commit_path_bench)
    // is the killer — it hit all three failure modes simultaneously
    // and paid ~2× the single-threaded cost in classifier overhead.
    constexpr std::size_t kShardedMinCommands = 256;
    const std::size_t chunkCount = storage.archetypes().chunks().size();
    if (totalCommands < kShardedMinCommands ||
        totalValueOnly == 0 ||
        chunkCount < 2) {
        for (auto& cb : buffers) commitBuffer(cb);
        // SHARDED_OPTIMISATION.md S0 — fallback path bookkeeping. Pass
        // A/B/C ns stay zero; `nsTotal` captures the serial-commit cost.
        const auto bdT1 = bdClock::now();
        commitBreakdown_.chunkCount     = chunkCount;
        commitBreakdown_.nsTotal       += bdElapsedNs(bdT0, bdT1);
        commitBreakdown_.fallbackCalls += 1;
        return;
    }

    // ----- Pass A: extend the migrating-entity set ---------------------
    //
    // §3.9.6 batch 21 — replaced `std::unordered_set<EntityHandle>`
    // with an engine-owned `std::vector<uint8_t>` bitmap keyed by
    // `EntityHandle::index`. Set + lookup are now single-byte indexed
    // reads; the buffer is preserved across calls so the steady-state
    // pays zero allocations after the first tick.
    //
    // SHARDED_OPTIMISATION.md S8 — the bitmap is now cumulative across
    // the wave (cleared in `step()` at wave start). Pass A only
    // adds to it; cross-system migrations within a wave thus stay
    // visible to a later system's bucket-demotion pass. Routing-
    // active buffers walk just `globalIndices()` (a small fraction
    // of their commands); legacy buffers fall back to the full scan.
    const std::size_t slotCap = storage.slotCount();
    if (shardMigratingBitmap_.size() < slotCap) {
        shardMigratingBitmap_.resize(slotCap, 0);
    }
    const std::size_t passAIndicesBefore = shardMigratingIndices_.size();

    auto markMigrating = [&](const detail::Command& cmd) {
        const EntityHandle e = commandTargetEntity(cmd);
        if (!e.valid() || e.index >= shardMigratingBitmap_.size()) return;
        if (shardMigratingBitmap_[e.index] == 0) {
            shardMigratingBitmap_[e.index] = 1;
            shardMigratingIndices_.push_back(e.index);
        }
    };

    const auto bdAStart = bdClock::now();
    // Skip the migrating-set scan entirely when every command is
    // value-only (totalValueOnly == totalCommands). The bitmap stays
    // empty and Pass B's `migrating[idx]` check always reads 0.
    if (totalValueOnly < totalCommands) {
        for (const auto& cb : buffers) {
            if (cb.routingActive()) {
                // S8 — globalIdx_ is exactly the migrating-or-stale
                // index list, populated at record time. Skip the
                // variant-tag scan over the value-only majority.
                const auto& cmds = cb.commands();
                for (auto gi : cb.globalIndices()) {
                    markMigrating(cmds[gi]);
                }
            } else {
                for (const auto& cmd : cb.commands()) {
                    if (commandIsMigrating(cmd)) markMigrating(cmd);
                }
            }
        }
    }
    const auto bdAEnd = bdClock::now();
    commitBreakdown_.nsPassA        += bdElapsedNs(bdAStart, bdAEnd);
    commitBreakdown_.migratingCount +=
        shardMigratingIndices_.size() - passAIndicesBefore;

    // ----- Pass B: classify in submission order, applying global cmds --
    //
    // §3.9.6 batch 21 — `shardChunkBins_` is engine-owned; we clear
    // the per-archetype bins (preserving their allocations) and resize
    // up to the current chunk count.
    if (shardChunkBins_.size() < chunkCount) {
        shardChunkBins_.resize(chunkCount);
    }
    for (auto& bin : shardChunkBins_) bin.clear();

    const auto bdBStart = bdClock::now();
    std::uint64_t bdGlobalLane = 0;
    std::uint64_t bdBinned     = 0;
    std::uint64_t bdBatchedMigrations = 0;
    const auto& chunks = storage.archetypes().chunks();

    // SHARDED_OPTIMISATION.md S6 — Same-kind migration run detector for
    // Pass B's global lane. Mirrors `commitBuffer`'s logic: when a
    // contiguous run of migrating commands in a single buffer all
    // target the same (srcArch, dstMask) pair, route them through
    // `setMaskAndMigrateBatch`. Run length below `kBatchMigrateThreshold`
    // falls through to one-by-one apply (the batch path's
    // setup-walk-handles cost exceeds the savings on tiny runs).
    const std::size_t kBatchMigrateThreshold = cfg_.batchMigrateThreshold;

    auto tryBatchMigrate = [&](auto& cmdsVec, std::size_t startIdx,
                               std::size_t runLen, auto predictDst,
                               auto applyValue) -> bool {
        if (runLen < kBatchMigrateThreshold) return false;
        const auto firstE = commandTargetEntity(cmdsVec[startIdx]);
        if (!firstE.valid()) return false;
        const auto firstLoc = storage.locate(firstE);
        if (firstLoc.archetype >= chunks.size()) return false;
        const ComponentSet srcMask = chunks[firstLoc.archetype].mask;
        const ComponentSet dstMask = predictDst(srcMask);
        if (dstMask == srcMask) return false;

        batchHandlesScratch_.clear();
        batchHandlesScratch_.reserve(runLen);
        for (std::size_t j = startIdx; j < startIdx + runLen; ++j) {
            const auto e = commandTargetEntity(cmdsVec[j]);
            if (!e.valid()) return false;
            const auto loc = storage.locate(e);
            if (loc.archetype != firstLoc.archetype) return false;
            batchHandlesScratch_.push_back(e);
        }
        if (!storage.setMaskAndMigrateBatch(batchHandlesScratch_, dstMask)) {
            return false;
        }
        const bool legacyHash = cfg_.legacyCommitHash;
        for (std::size_t j = 0; j < runLen; ++j) {
            applyValue(cmdsVec[startIdx + j], batchHandlesScratch_[j]);
            if (legacyHash) {
                commitHashAcc_ = hashCommandImpl(commitHashAcc_,
                    cmdsVec[startIdx + j], kInvalidEntity);
            }
        }
        bdBatchedMigrations += runLen;
        return true;
    };

    // SHARDED_OPTIMISATION.md S8 — same batch-migrate logic as
    // `tryBatchMigrate`, but the run is a span of command indices
    // INTO cmdsVec rather than a contiguous (startIdx, runLen) slice.
    // Used by the routing-active fast path where `globalIdx_` lists
    // non-contiguous indices in submission order.
    auto tryBatchMigrateIndexed = [&](auto& cmdsVec,
                                      std::span<const std::uint32_t> cmdIndices,
                                      auto predictDst,
                                      auto applyValue) -> bool {
        const std::size_t runLen = cmdIndices.size();
        if (runLen < kBatchMigrateThreshold) return false;
        const auto firstE = commandTargetEntity(cmdsVec[cmdIndices[0]]);
        if (!firstE.valid()) return false;
        const auto firstLoc = storage.locate(firstE);
        if (firstLoc.archetype >= chunks.size()) return false;
        const ComponentSet srcMask = chunks[firstLoc.archetype].mask;
        const ComponentSet dstMask = predictDst(srcMask);
        if (dstMask == srcMask) return false;

        batchHandlesScratch_.clear();
        batchHandlesScratch_.reserve(runLen);
        for (auto ci : cmdIndices) {
            const auto e = commandTargetEntity(cmdsVec[ci]);
            if (!e.valid()) return false;
            const auto loc = storage.locate(e);
            if (loc.archetype != firstLoc.archetype) return false;
            batchHandlesScratch_.push_back(e);
        }
        if (!storage.setMaskAndMigrateBatch(batchHandlesScratch_, dstMask)) {
            return false;
        }
        const bool legacyHash = cfg_.legacyCommitHash;
        for (std::size_t j = 0; j < runLen; ++j) {
            applyValue(cmdsVec[cmdIndices[j]], batchHandlesScratch_[j]);
            if (legacyHash) {
                commitHashAcc_ = hashCommandImpl(commitHashAcc_,
                    cmdsVec[cmdIndices[j]], kInvalidEntity);
            }
        }
        bdBatchedMigrations += runLen;
        return true;
    };

    for (auto& cb : buffers) {
        auto& cmdsVec = cb.commands();

        // SHARDED_OPTIMISATION.md S8 — record-time per-chunk routing
        // fast path. `cmdsVec` is the authoritative submission-order
        // command stream; the buffer's `chunkBuckets()` and
        // `globalIndices()` lists are index-into-cmdsVec partitions
        // populated at record time. We pay the per-cmd locate() once
        // (at record time, cache-hot against the slot table) rather
        // than once per cmd in Pass B. Cross-system stale hints are
        // demoted via the cumulative migrating bitmap built in Pass A.
        if (cb.routingActive()) {
            const auto& gIdx    = cb.globalIndices();
            const auto& buckets = cb.chunkBuckets();

            // Walk buckets: each entry either transfers to engine
            // shardChunkBins_ (its hint is still valid) or gets
            // demoted (its target entity migrated this wave).
            demotedScratch_.clear();
            for (std::size_t k = 0; k < buckets.size(); ++k) {
                const auto& bucket = buckets[k];
                if (bucket.empty()) continue;
                if (k >= shardChunkBins_.size()) {
                    shardChunkBins_.resize(k + 1);
                }
                auto& dst = shardChunkBins_[k];
                dst.reserve(dst.size() + bucket.size());
                for (auto idx : bucket) {
                    const EntityHandle e = commandTargetEntity(cmdsVec[idx]);
                    const bool isMig =
                        e.valid() && e.index < shardMigratingBitmap_.size() &&
                        shardMigratingBitmap_[e.index] != 0;
                    if (isMig) {
                        demotedScratch_.push_back(idx);
                        continue;
                    }
                    dst.push_back(&cmdsVec[idx]);
                    if (cfg_.legacyCommitHash) {
                        commitHashAcc_ = hashCommandImpl(commitHashAcc_,
                            cmdsVec[idx], kInvalidEntity);
                    }
                    ++bdBinned;
                }
            }
            // Buckets are walked in chunk order; the resulting
            // demoted indices may be out of submission order. Sort
            // so the merge below preserves it.
            std::sort(demotedScratch_.begin(), demotedScratch_.end());

            // Merge-apply globalIdx_ + demotedScratch_ in submission
            // order. Runs of same-kind migrating commands inside
            // gIdx[] are batched via `tryBatchMigrateIndexed`, but a
            // run is truncated if a demoted index falls inside it
            // (keeps the merged stream in strict submission order).
            std::size_t gi = 0, di = 0;
            while (gi < gIdx.size() || di < demotedScratch_.size()) {
                const bool pickG =
                    gi < gIdx.size() &&
                    (di >= demotedScratch_.size() ||
                     gIdx[gi] < demotedScratch_[di]);
                if (!pickG) {
                    const std::uint32_t demIdx = demotedScratch_[di];
                    ++di;
                    const EntityHandle spawnResult =
                        applyCommandImpl(cmdsVec[demIdx], storage);
                    if (cfg_.legacyCommitHash) {
                        commitHashAcc_ = hashCommandImpl(commitHashAcc_,
                            cmdsVec[demIdx], spawnResult);
                    }
                    ++bdGlobalLane;
                    continue;
                }
                auto& cmd = cmdsVec[gIdx[gi]];

                // Run-detect: extend within gIdx, then truncate so
                // every batched index is < the next demoted index
                // (preserves submission order in the merged stream).
                std::size_t runEndInG = gi + 1;
                bool batched = false;
                auto truncateAgainstDemoted = [&](std::size_t r) -> std::size_t {
                    if (di >= demotedScratch_.size()) return r;
                    const std::uint32_t nextDem = demotedScratch_[di];
                    while (r > gi + 1 && gIdx[r - 1] >= nextDem) --r;
                    return r;
                };

                if (auto* addP = std::get_if<detail::CmdAddTag>(&cmd)) {
                    const Component tag = addP->tag;
                    while (runEndInG < gIdx.size()) {
                        auto* q = std::get_if<detail::CmdAddTag>(
                            &cmdsVec[gIdx[runEndInG]]);
                        if (!q || q->tag != tag) break;
                        ++runEndInG;
                    }
                    runEndInG = truncateAgainstDemoted(runEndInG);
                    auto runSpan = std::span<const std::uint32_t>(
                        gIdx.data() + gi, runEndInG - gi);
                    batched = tryBatchMigrateIndexed(cmdsVec, runSpan,
                        [tag](ComponentSet src) {
                            ComponentSet d = src; d.add(tag); return d;
                        },
                        [](detail::Command&, EntityHandle) {});
                } else if (auto* remP = std::get_if<detail::CmdRemoveTag>(&cmd)) {
                    const Component tag = remP->tag;
                    while (runEndInG < gIdx.size()) {
                        auto* q = std::get_if<detail::CmdRemoveTag>(
                            &cmdsVec[gIdx[runEndInG]]);
                        if (!q || q->tag != tag) break;
                        ++runEndInG;
                    }
                    runEndInG = truncateAgainstDemoted(runEndInG);
                    auto runSpan = std::span<const std::uint32_t>(
                        gIdx.data() + gi, runEndInG - gi);
                    batched = tryBatchMigrateIndexed(cmdsVec, runSpan,
                        [tag](ComponentSet src) {
                            ComponentSet d = src; d.remove(tag); return d;
                        },
                        [](detail::Command&, EntityHandle) {});
                } else if (std::holds_alternative<detail::CmdSetHealth>(cmd)) {
                    while (runEndInG < gIdx.size() &&
                           std::holds_alternative<detail::CmdSetHealth>(
                               cmdsVec[gIdx[runEndInG]])) {
                        ++runEndInG;
                    }
                    runEndInG = truncateAgainstDemoted(runEndInG);
                    auto runSpan = std::span<const std::uint32_t>(
                        gIdx.data() + gi, runEndInG - gi);
                    batched = tryBatchMigrateIndexed(cmdsVec, runSpan,
                        [](ComponentSet src) {
                            ComponentSet d = src; d.add(Component::Health); return d;
                        },
                        [&storage](detail::Command& c, EntityHandle e) {
                            if (auto* p = storage.mutHealth(e)) {
                                *p = std::get<detail::CmdSetHealth>(c).value;
                            }
                        });
                } else if (std::holds_alternative<detail::CmdSetFaction>(cmd)) {
                    while (runEndInG < gIdx.size() &&
                           std::holds_alternative<detail::CmdSetFaction>(
                               cmdsVec[gIdx[runEndInG]])) {
                        ++runEndInG;
                    }
                    runEndInG = truncateAgainstDemoted(runEndInG);
                    auto runSpan = std::span<const std::uint32_t>(
                        gIdx.data() + gi, runEndInG - gi);
                    batched = tryBatchMigrateIndexed(cmdsVec, runSpan,
                        [](ComponentSet src) {
                            ComponentSet d = src; d.add(Component::Faction); return d;
                        },
                        [&storage](detail::Command& c, EntityHandle e) {
                            if (auto* p = storage.mutFaction(e)) {
                                *p = std::get<detail::CmdSetFaction>(c).value;
                            }
                        });
                } else if (std::holds_alternative<detail::CmdSetBoundingVolume>(cmd)) {
                    while (runEndInG < gIdx.size() &&
                           std::holds_alternative<detail::CmdSetBoundingVolume>(
                               cmdsVec[gIdx[runEndInG]])) {
                        ++runEndInG;
                    }
                    runEndInG = truncateAgainstDemoted(runEndInG);
                    auto runSpan = std::span<const std::uint32_t>(
                        gIdx.data() + gi, runEndInG - gi);
                    batched = tryBatchMigrateIndexed(cmdsVec, runSpan,
                        [](ComponentSet src) {
                            ComponentSet d = src; d.add(Component::BoundingVolume); return d;
                        },
                        [&storage](detail::Command& c, EntityHandle e) {
                            if (auto* p = storage.mutBoundingVolume(e)) {
                                *p = std::get<detail::CmdSetBoundingVolume>(c).value;
                            }
                        });
                }

                if (batched) {
                    gi = runEndInG;
                } else {
                    const EntityHandle spawnResult =
                        applyCommandImpl(cmd, storage);
                    if (cfg_.legacyCommitHash) {
                        commitHashAcc_ = hashCommandImpl(commitHashAcc_,
                            cmd, spawnResult);
                    }
                    ++bdGlobalLane;
                    ++gi;
                }
            }
            continue; // next buffer; skip the legacy Pass B body below
        }

        std::size_t i = 0;
        while (i < cmdsVec.size()) {
            auto& cmd = cmdsVec[i];
            // Value-only chunk-local fast path: bin and hash, no apply yet.
            if (!commandIsMigrating(cmd)) {
                const EntityHandle e = commandTargetEntity(cmd);
                const bool isMigrating =
                    e.valid() && e.index < shardMigratingBitmap_.size() &&
                    shardMigratingBitmap_[e.index] != 0;
                if (e.valid() && !isMigrating) {
                    const auto loc = storage.locate(e);
                    // locate() returns {max,max} for stale handles —
                    // bounds check against the live chunk count
                    // filters those out (they'll fall through to the
                    // global lane, where applyCommandImpl's mut*()
                    // safely no-ops on stale handles).
                    if (loc.archetype < chunks.size()) {
                        if (loc.archetype >= shardChunkBins_.size()) {
                            shardChunkBins_.resize(loc.archetype + 1);
                        }
                        shardChunkBins_[loc.archetype].push_back(&cmd);
                        // §3.6 batch 30 — per-command hash mix only
                        // contributes to the legacy hash. Under the
                        // new path, the chunk this command will touch
                        // is already marked dirty by `mut*()` in
                        // pass C; finalizeCommitHash will roll it up.
                        if (cfg_.legacyCommitHash) {
                            commitHashAcc_ = hashCommandImpl(commitHashAcc_,
                                cmd, kInvalidEntity);
                        }
                        ++bdBinned;
                        ++i;
                        continue;
                    }
                }
            }

            // Global lane. Try to extend the run for batch migrate.
            std::size_t runEnd = i + 1;
            bool batched = false;
            if (auto* addP = std::get_if<detail::CmdAddTag>(&cmd)) {
                const Component tag = addP->tag;
                while (runEnd < cmdsVec.size()) {
                    auto* q = std::get_if<detail::CmdAddTag>(&cmdsVec[runEnd]);
                    if (!q || q->tag != tag) break;
                    ++runEnd;
                }
                batched = tryBatchMigrate(cmdsVec, i, runEnd - i,
                    [tag](ComponentSet src) {
                        ComponentSet d = src; d.add(tag); return d;
                    },
                    [](detail::Command&, EntityHandle) {});
            } else if (auto* remP = std::get_if<detail::CmdRemoveTag>(&cmd)) {
                const Component tag = remP->tag;
                while (runEnd < cmdsVec.size()) {
                    auto* q = std::get_if<detail::CmdRemoveTag>(&cmdsVec[runEnd]);
                    if (!q || q->tag != tag) break;
                    ++runEnd;
                }
                batched = tryBatchMigrate(cmdsVec, i, runEnd - i,
                    [tag](ComponentSet src) {
                        ComponentSet d = src; d.remove(tag); return d;
                    },
                    [](detail::Command&, EntityHandle) {});
            } else if (std::holds_alternative<detail::CmdSetHealth>(cmd)) {
                while (runEnd < cmdsVec.size() &&
                       std::holds_alternative<detail::CmdSetHealth>(cmdsVec[runEnd])) {
                    ++runEnd;
                }
                batched = tryBatchMigrate(cmdsVec, i, runEnd - i,
                    [](ComponentSet src) {
                        ComponentSet d = src; d.add(Component::Health); return d;
                    },
                    [&storage](detail::Command& c, EntityHandle e) {
                        if (auto* p = storage.mutHealth(e)) {
                            *p = std::get<detail::CmdSetHealth>(c).value;
                        }
                    });
            } else if (std::holds_alternative<detail::CmdSetFaction>(cmd)) {
                while (runEnd < cmdsVec.size() &&
                       std::holds_alternative<detail::CmdSetFaction>(cmdsVec[runEnd])) {
                    ++runEnd;
                }
                batched = tryBatchMigrate(cmdsVec, i, runEnd - i,
                    [](ComponentSet src) {
                        ComponentSet d = src; d.add(Component::Faction); return d;
                    },
                    [&storage](detail::Command& c, EntityHandle e) {
                        if (auto* p = storage.mutFaction(e)) {
                            *p = std::get<detail::CmdSetFaction>(c).value;
                        }
                    });
            } else if (std::holds_alternative<detail::CmdSetBoundingVolume>(cmd)) {
                while (runEnd < cmdsVec.size() &&
                       std::holds_alternative<detail::CmdSetBoundingVolume>(cmdsVec[runEnd])) {
                    ++runEnd;
                }
                batched = tryBatchMigrate(cmdsVec, i, runEnd - i,
                    [](ComponentSet src) {
                        ComponentSet d = src; d.add(Component::BoundingVolume); return d;
                    },
                    [&storage](detail::Command& c, EntityHandle e) {
                        if (auto* p = storage.mutBoundingVolume(e)) {
                            *p = std::get<detail::CmdSetBoundingVolume>(c).value;
                        }
                    });
            }

            if (!batched) {
                for (std::size_t j = i; j < runEnd; ++j) {
                    const EntityHandle spawnResult =
                        applyCommandImpl(cmdsVec[j], storage);
                    if (cfg_.legacyCommitHash) {
                        commitHashAcc_ = hashCommandImpl(commitHashAcc_,
                            cmdsVec[j], spawnResult);
                    }
                }
                bdGlobalLane += (runEnd - i);
            }
            i = runEnd;
        }
    }
    const auto bdBEnd = bdClock::now();
    commitBreakdown_.nsPassB            += bdElapsedNs(bdBStart, bdBEnd);
    commitBreakdown_.globalLaneApplied  += bdGlobalLane + bdBatchedMigrations;
    commitBreakdown_.batchedMigrations  += bdBatchedMigrations;
    commitBreakdown_.binnedValueOnly    += bdBinned;
    commitBreakdown_.chunkCount          = chunkCount;

    // ----- Pass C: apply chunk bins in parallel ------------------------
    //
    // Each non-empty bin is submitted as one job; bins target disjoint
    // archetype chunks so their writes never overlap. The mut*()
    // setters look up by `EntityHandle`, so an entity that was
    // swap-popped within its chunk by a pass-B global migrate still
    // resolves correctly.
    std::size_t activeBins = 0;
    for (const auto& bin : shardChunkBins_) {
        if (!bin.empty()) ++activeBins;
    }
    commitBreakdown_.activeBins   += activeBins;
    commitBreakdown_.shardedCalls += 1;
    if (activeBins == 0) {
        for (auto& cb : buffers) cb.clear();
        // S8 — bitmap is wave-cumulative; cleared at wave start in
        // `step()` rather than here.
        const auto bdT1 = bdClock::now();
        commitBreakdown_.nsTotal += bdElapsedNs(bdT0, bdT1);
        return;
    }

    // SHARDED_OPTIMISATION.md S5 — small-bin serial fast path.
    //
    // S0 measured `JobLatch::wait` as 99% of Pass C on every value-only
    // workload; the dispatch + wake overhead dwarfs the actual apply
    // work for bins below a few hundred commands. Split bins into two
    // lanes: bins ≥ `kMinBinForJob` get a worker job (parallelism still
    // earns its keep at this size); bins below it execute on the sim
    // thread, often overlapping with workers already running the large
    // ones. When no bin meets the threshold we skip the latch entirely
    // — the mutex + CV cost vanishes for small-aggregate calls.
    //
    // Determinism: bins target disjoint archetype chunks (Pass B
    // already routed by `storage.locate(e).archetype`), so the order
    // small-vs-large bins finish in doesn't affect any chunk's content,
    // only the per-chunk hash rollup time. `finalizeCommitHash` sorts
    // chunks by `mask.bits()` before folding, so the rollup result is
    // independent of bin execution order.
    constexpr std::size_t kMinBinForJob = 256;
    std::size_t largeBins = 0;
    std::size_t largestBinIdx = shardChunkBins_.size(); // sentinel
    std::size_t largestBinSize = 0;
    for (std::size_t k = 0; k < shardChunkBins_.size(); ++k) {
        const auto sz = shardChunkBins_[k].size();
        if (sz >= kMinBinForJob) {
            ++largeBins;
            if (sz > largestBinSize) {
                largestBinSize = sz;
                largestBinIdx  = k;
            }
        }
    }
    const std::size_t inlineBins = activeBins - largeBins;
    commitBreakdown_.inlineBinCount += inlineBins;

    // SHARDED_OPTIMISATION.md S9 — sim-thread-inline largest bin.
    //
    // Pre-S9 Pass C: every large bin → worker job; sim thread runs
    // small bins inline then waits on the latch. S0 found that
    // `JobLatch::wait` was ~99% of Pass C on workloads where Pass C
    // mattered — workers chewed the large bins while the sim thread
    // sat idle, so Pass C's wall-clock equalled
    // `max(workers' bin completion times)`.
    //
    // S9 picks the largest large bin to run inline on the sim thread
    // alongside the small-bin lane and dispatches the rest (largeBins
    // − 1) to workers. For evenly-balanced workloads (MultiArch is the
    // canary: 4 bins × ~25k cmds × 4 workers → sim was idle waiting),
    // the latch wait drops to roughly zero because the sim is now a
    // peer of the workers, not their wait barrier.
    //
    // Determinism: bins target disjoint chunks; `finalizeCommitHash`
    // sorts by mask.bits before folding. Execution order doesn't
    // affect the hash regardless of which lane runs which bin.
    //
    // Opt-out: `Config::inlineLargestBin = false` (or
    // `THREADMAXX_NO_INLINE_LARGEST=1` env in benches) reverts to the
    // pre-S9 "every large bin → worker" lane.
    const bool inlineLargest =
        cfg_.inlineLargestBin && largestBinIdx < shardChunkBins_.size();

    // SHARDED_OPTIMISATION.md S10 — row-split the largest bin
    // (OPT-IN, off by default; see `Config::splitLargestBin` doc).
    //
    // Row-partitions the single largest large-bin into M sub-bins so
    // the total lane count (sim + workers) is saturated. Sub-bin 0
    // runs inline on sim, sub-bins [1..M) on workers.
    //
    // **The default is `false` because the bench shows S10 regresses
    // its canonical case by +207%.** The `std::visit` + `locate()`
    // pre-classification cost (~25–30 ns/cmd) is roughly double the
    // apply cost itself (~13.6 ns/cmd), so the partition pass
    // doubles per-cmd memory traffic on the slot table — strictly
    // slower than the S9 sim-inline lane. A viable revisit would
    // need record-time row-bucketing (analogous to S8's
    // chunkBuckets) to avoid the per-cmd classify cost. The code,
    // the knob, and `tests/pass_c_split_test.cpp` are preserved as
    // the fixed point for that future investigation.
    //
    // Determinism (still bit-exact when enabled): each cmd's target
    // entity's current row is looked up via `storage.locate(e)`;
    // the sub-bin index is `min(row / rowsPerBin, M-1)`. Sub-bins
    // target disjoint rows of the same archetype chunk — writes
    // never overlap. Two cmds on the SAME entity (and therefore the
    // same row) land in the same sub-bin, where submission order is
    // preserved.
    //
    // Eligibility: `inlineLargestBin` already fired AND
    // `largeBins == 1` AND `largestBinSize >= 2 * kMinBinForJob`.
    std::size_t splitFactor = 1;
    std::uint32_t splitRowsPerBin = 0;
    std::size_t latchedSubBins = 0;
    if (cfg_.splitLargestBin && inlineLargest
            && largeBins == 1
            && largestBinSize >= 2 * kMinBinForJob) {
        const std::size_t lanes =
            static_cast<std::size_t>(jobs_->workerCount()) + 1u;
        // `largeBins - 1` lanes already taken by OTHER large bins;
        // the rest go to split sub-bins of the largest.
        const std::size_t want =
            lanes > (largeBins - 1) ? (lanes - (largeBins - 1)) : 2u;
        const std::size_t maxSplits = largestBinSize / kMinBinForJob;
        splitFactor = want;
        if (splitFactor > maxSplits) splitFactor = maxSplits;
        if (splitFactor < 2) splitFactor = 1;
        if (splitFactor >= 2) {
            const auto& largestChunk =
                storage.archetypes().chunks()[largestBinIdx];
            const std::uint32_t rowCount = largestChunk.size();
            if (rowCount == 0) {
                splitFactor = 1;
            } else {
                splitRowsPerBin =
                    (rowCount + static_cast<std::uint32_t>(splitFactor) - 1u)
                    / static_cast<std::uint32_t>(splitFactor);
                if (shardSubBins_.size() < splitFactor) {
                    shardSubBins_.resize(splitFactor);
                }
                for (std::size_t i = 0; i < splitFactor; ++i) {
                    shardSubBins_[i].clear();
                }
                for (auto* cmd : shardChunkBins_[largestBinIdx]) {
                    const auto e = commandTargetEntity(*cmd);
                    const auto loc = storage.locate(e);
                    std::size_t subIdx = static_cast<std::size_t>(
                        loc.row / splitRowsPerBin);
                    if (subIdx >= splitFactor) subIdx = splitFactor - 1;
                    shardSubBins_[subIdx].push_back(cmd);
                }
                for (std::size_t i = 1; i < splitFactor; ++i) {
                    if (!shardSubBins_[i].empty()) ++latchedSubBins;
                }
                ++commitBreakdown_.splitLargestApplied;
            }
        }
    }
    const bool doSplit = splitFactor >= 2;

    // When doSplit, sim takes sub-bin 0; workers take sub-bins
    // [1..M) (non-empty count = `latchedSubBins`) plus the other
    // `largeBins - 1` large bins. Without S10 we keep the pre-S10
    // count (`inlineLargest ? largeBins - 1 : largeBins`).
    const std::size_t latchedJobs = doSplit
        ? (largeBins - 1) + latchedSubBins
        : (inlineLargest ? (largeBins - 1) : largeBins);

    const auto bdCStart = bdClock::now();
    if (largeBins == 0) {
        // No bin worth a job — run every non-empty bin inline. No
        // `JobLatch`, no worker wake, no mutex/CV traffic at all.
        for (auto& bin : shardChunkBins_) {
            if (bin.empty()) continue;
            for (auto* cmd : bin) {
                applyCommandImpl(*cmd, storage);
            }
        }
    } else if (latchedJobs == 0) {
        // S9 — single large bin (or only the sim-thread's inline target
        // qualifies): no worker dispatch needed. Sim thread runs the
        // largest bin AND the small bins inline. No latch traffic.
        if (inlineLargest) {
            if (doSplit) {
                // No worker eligible → sim does every sub-bin.
                for (std::size_t i = 0; i < splitFactor; ++i) {
                    for (auto* cmd : shardSubBins_[i]) {
                        applyCommandImpl(*cmd, storage);
                    }
                }
            } else {
                for (auto* cmd : shardChunkBins_[largestBinIdx]) {
                    applyCommandImpl(*cmd, storage);
                }
            }
            ++commitBreakdown_.inlineLargestApplied;
        }
        for (auto& bin : shardChunkBins_) {
            if (bin.empty() || bin.size() >= kMinBinForJob) continue;
            for (auto* cmd : bin) {
                applyCommandImpl(*cmd, storage);
            }
        }
    } else {
        JobLatch done(static_cast<std::ptrdiff_t>(latchedJobs));
        for (std::size_t k = 0; k < shardChunkBins_.size(); ++k) {
            auto& bin = shardChunkBins_[k];
            if (bin.size() < kMinBinForJob) continue;
            if (inlineLargest && k == largestBinIdx) continue;
            jobs_->submit([&bin, &done, &storage] {
                for (auto* cmd : bin) {
                    applyCommandImpl(*cmd, storage);
                }
                done.count_down();
            });
        }
        // S10 — dispatch sub-bins [1..M) of the row-split largest bin.
        if (doSplit) {
            for (std::size_t i = 1; i < splitFactor; ++i) {
                if (shardSubBins_[i].empty()) continue;
                auto& subbin = shardSubBins_[i];
                jobs_->submit([&subbin, &done, &storage] {
                    for (auto* cmd : subbin) {
                        applyCommandImpl(*cmd, storage);
                    }
                    done.count_down();
                });
            }
        }
        // Sim-thread inline lane: run the largest large bin first
        // (S9), THEN the small bins. Doing the largest first means
        // its work overlaps maximally with the workers' large bins.
        // Under S10, sim runs sub-bin 0 only (the rest are on
        // workers).
        if (inlineLargest) {
            if (doSplit) {
                for (auto* cmd : shardSubBins_[0]) {
                    applyCommandImpl(*cmd, storage);
                }
            } else {
                for (auto* cmd : shardChunkBins_[largestBinIdx]) {
                    applyCommandImpl(*cmd, storage);
                }
            }
            ++commitBreakdown_.inlineLargestApplied;
        }
        for (auto& bin : shardChunkBins_) {
            if (bin.empty() || bin.size() >= kMinBinForJob) continue;
            for (auto* cmd : bin) {
                applyCommandImpl(*cmd, storage);
            }
        }
        const auto bdWaitStart = bdClock::now();
        done.wait();
        commitBreakdown_.nsLatchWait += bdElapsedNs(bdWaitStart, bdClock::now());
    }
    const auto bdCEnd = bdClock::now();
    commitBreakdown_.nsPassC += bdElapsedNs(bdCStart, bdCEnd);

    for (auto& cb : buffers) cb.clear();
    // S8 — bitmap is wave-cumulative; cleared at wave start in
    // `step()` (not here). Indices set in Pass A persist so a later
    // commit this wave can still demote a value-only cmd targeting
    // an entity that an earlier commit migrated.

    const auto bdT1 = bdClock::now();
    commitBreakdown_.nsTotal += bdElapsedNs(bdT0, bdT1);
}

// §3.6 batch 30 — end-of-step per-archetype hash rollup. Refreshes
// every dirty chunk's `cachedHash` (parallel across chunks when ≥ 2
// chunks need work), then combines all chunks' cachedHashes into one
// FNV-1a-64 running hash, walking chunks in mask-bits ascending order
// so the result is independent of archetype creation order.
//
// Called from `step()` only when `cfg_.legacyCommitHash == false`.
// The legacy path keeps `commitHashAcc_` as the running per-command
// mix and skips this entirely.
std::uint64_t EngineImpl::finalizeCommitHash() {
    auto& storage = world_.impl_().storage;
    auto& chunks  = storage.archetypes().chunks();

    // Collect indices of dirty chunks. Stable order (creation order);
    // we re-sort for the combine pass below.
    std::vector<std::uint32_t> dirty;
    dirty.reserve(chunks.size());
    for (std::uint32_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].hashDirty) dirty.push_back(i);
    }

    // Recompute dirty chunks. ≥ 2 dirty chunks → parallel via jobs_;
    // each job writes to its own chunk's cachedHash so there's no
    // sharing. Single-chunk falls through to a serial recompute.
    if (dirty.size() >= 2 && jobs_) {
        JobLatch done(static_cast<std::ptrdiff_t>(dirty.size()));
        for (std::uint32_t idx : dirty) {
            ArchetypeChunk* c = &chunks[idx];
            jobs_->submit([c, &done] {
                c->cachedHash = hashChunkContent(*c);
                c->hashDirty  = false;
                done.count_down();
            });
        }
        done.wait();
    } else {
        for (std::uint32_t idx : dirty) {
            auto& c = chunks[idx];
            c.cachedHash = hashChunkContent(c);
            c.hashDirty  = false;
        }
    }

    // Combine: sort by mask.bits() so the final hash is invariant under
    // archetype creation order (two engines that ended up with the same
    // set of chunks but created them in different orders produce the
    // same commitHash). Empty chunks contribute their mask + 0-count
    // fingerprint, distinguishing engines whose archetype inventories
    // differ.
    std::vector<std::uint32_t> order(chunks.size());
    std::iota(order.begin(), order.end(), std::uint32_t{0});
    std::sort(order.begin(), order.end(),
              [&chunks](std::uint32_t a, std::uint32_t b) {
                  return chunks[a].mask.bits() < chunks[b].mask.bits();
              });
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::uint32_t idx : order) {
        h = mixHashBytes(h, chunks[idx].cachedHash);
    }
    return h;
}

void EngineImpl::buildRenderFrame() {
    const unsigned back = 1u - frontIndex_.load(std::memory_order_acquire);
    auto& dst = renderInstanceBuffers_[back];
    auto& prev = renderInstancePrev_[back];
    dst.clear();
    prev.clear();

    // §3.1 batch 6: walk archetype chunks rather than the legacy
    // stitched dense view — the chunk's mask is checked once per chunk
    // (skipping RenderTag-less and Disabled archetypes wholesale)
    // instead of a per-entity test inside the loop.
    const auto& chunks = world_.impl_().storage.archetypes().chunks();
    std::size_t reserveHint = 0;
    for (const auto& c : chunks) {
        if (!c.mask.has(Component::RenderTag)) continue;
        if (c.mask.has(Component::DisabledTag)) continue;
        reserveHint += c.entities.size();
    }
    dst.reserve(reserveHint);
    prev.reserve(reserveHint);
    for (const auto& c : chunks) {
        if (!c.mask.has(Component::RenderTag)) continue;
        if (c.mask.has(Component::DisabledTag)) continue;
        const bool hasUserData = c.mask.has(Component::UserData);
        const auto rows = c.entities.size();
        for (std::size_t i = 0; i < rows; ++i) {
            dst.push_back(RenderInstance{
                c.entities[i],
                c.transforms[i],
                c.renderTags[i].meshId,
                c.renderTags[i].materialId,
                c.renderTags[i].flags,
                hasUserData ? c.userData[i].value : 0,
            });
            // §3.6.5 batch 15a — pair each instance with its
            // previous-tick transform. New entities (not in the
            // map yet) get their current transform, giving a
            // clean lerp(prev, current, alpha) with no special
            // case on first-frame spawn.
            //
            // §3.10.2 batch 22 — F7 fix. Vector lookup by entity
            // index + generation guard, no hash-map overhead.
            const auto handle = c.entities[i];
            const Transform* prevT = nullptr;
            if (handle.index < prevTransformByIndex_.size() &&
                prevTransformGenByIndex_[handle.index] == handle.generation) {
                prevT = &prevTransformByIndex_[handle.index];
            }
            prev.push_back(RenderInstancePrev{
                handle, prevT ? *prevT : c.transforms[i]});
        }
    }

    // §3.2 batch 8: merge per-system RenderFrameBuilder slices into the
    // back storage in registration order. The published RenderFrame
    // exposes spans pointing into this storage.
    auto& hier = renderFrameStorage_[back];
    hier.clear();
    for (const auto& builder : systemRenderBuilders_) {
        const auto cams = builder.cameras();
        hier.cameras.insert(hier.cameras.end(), cams.begin(), cams.end());
        const auto lts = builder.lights();
        hier.lights.insert(hier.lights.end(), lts.begin(), lts.end());
        for (std::size_t p = 0; p < kRenderPassCount; ++p) {
            const auto items = builder.drawItems(
                static_cast<RenderPass>(p));
            hier.drawItems[p].insert(hier.drawItems[p].end(),
                                     items.begin(), items.end());
        }
        const auto dl = builder.debugLines();
        hier.debugLines.insert(hier.debugLines.end(), dl.begin(), dl.end());
        const auto dp = builder.debugPoints();
        hier.debugPoints.insert(hier.debugPoints.end(),
                                dp.begin(), dp.end());
        const auto dt = builder.debugText();
        hier.debugText.insert(hier.debugText.end(), dt.begin(), dt.end());
    }

    auto& frame = renderFrames_[back];
    frame.tick = tick_;
    frame.simulationTime = simulationTime_;
    frame.deltaTime = cfg_.fixedStepSeconds;
    frame.alpha = 0.0f;
    frame.instances = std::span<const RenderInstance>(dst.data(), dst.size());
    frame.prevTransforms = std::span<const RenderInstancePrev>(
        prev.data(), prev.size());
    frame.cameras = std::span<const Camera>(hier.cameras.data(),
                                            hier.cameras.size());
    frame.lights  = std::span<const Light>(hier.lights.data(),
                                           hier.lights.size());
    for (std::size_t p = 0; p < kRenderPassCount; ++p) {
        frame.drawItems[p] = std::span<const DrawItem>(
            hier.drawItems[p].data(), hier.drawItems[p].size());
    }
    frame.debugLines  = std::span<const DebugLine>(
        hier.debugLines.data(), hier.debugLines.size());
    frame.debugPoints = std::span<const DebugPoint>(
        hier.debugPoints.data(), hier.debugPoints.size());
    frame.debugText   = std::span<const DebugText>(
        hier.debugText.data(), hier.debugText.size());

    // §3.6.5 batch 15a — refresh the previous-transform map from this
    // tick's instances. Next tick's `buildRenderFrame` will use this
    // as the "prev" for its own instances.
    //
    // §3.10.2 batch 22 — F7 fix. Flat vector keyed by entity index +
    // a parallel generation vector for the (index, generation)
    // sentinel. Stale entries from prior ticks (entities that no
    // longer carry RenderTag, or have been destroyed and re-spawned
    // with a new generation) are filtered by the generation check on
    // read — no clear/rebuild dance, no hash-map allocations.
    for (const auto& inst : dst) {
        const auto idx = inst.entity.index;
        if (idx >= prevTransformByIndex_.size()) {
            prevTransformByIndex_.resize(idx + 1);
            prevTransformGenByIndex_.resize(idx + 1, 0);
        }
        prevTransformByIndex_[idx] = inst.transform;
        prevTransformGenByIndex_[idx] = inst.entity.generation;
    }

    frontIndex_.store(back, std::memory_order_release);
}

void EngineImpl::submitInterpolatedFrame(float alpha) {
    if (!renderer_) return;
    const unsigned front = frontIndex_.load(std::memory_order_acquire);
    // Mutating alpha on the front frame is safe: submitFrame runs
    // synchronously on the sim thread and the renderer must not retain
    // pointers past it.
    renderFrames_[front].alpha = alpha;
    renderer_->submitFrame(renderFrames_[front]);
}

void EngineImpl::step() {
    if (!initialized_) return;

    // Pause skips the entire simulation: tick does not advance, systems
    // are not run, no commits. Stats are reset to "this step did nothing"
    // so the HUD doesn't show stale numbers from before the pause.
    if (paused_.load(std::memory_order_acquire)) {
        for (auto& ss : systemStats_) {
            ss.lastUpdateSeconds = 0.0;
            ss.jobsSubmittedLastStep = 0;
            ss.commandsCommittedLastStep = 0;
            ss.buildRenderFrameSeconds = 0.0;
        }
        stats_.lastStepSeconds = 0.0;
        stats_.jobsSubmittedLastStep = 0;
        stats_.commandsCommittedLastStep = 0;
        stats_.commitDurationSeconds = 0.0;
        // §3.6 batch 13a: paused step did not commit anything, so the
        // hash is the FNV-1a-64 offset basis (the "no commits"
        // sentinel). Keeps the field stable across pause windows.
        stats_.commitHash = 0xcbf29ce484222325ull;
        return;
    }

    // ADAPTIVE_TUNING.md T4 — apply any policy-staged patch BEFORE
    // preStep so this tick's wave systems see the new grain. Patch
    // application is sim-thread serial; never mid-wave.
    applyPendingTuningPatch();

    const double dt = cfg_.fixedStepSeconds * timeScale_;
    const auto stepStart = std::chrono::steady_clock::now();

    // §3.7 batch 14 — publish step-start clock for the stall watchdog.
    // Relaxed atomics: watchdog only needs an eventually-consistent
    // read, not a synchronization point.
    {
        const std::uint64_t ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                stepStart.time_since_epoch()).count());
        watchdogStepStartNs_.store(ns, std::memory_order_relaxed);
        watchdogActiveTick_.store(tick_ + 1, std::memory_order_relaxed);
    }

    commandsThisStep_ = 0;
    commitSecondsThisStep_ = 0.0;
    std::uint64_t jobsThisStep = 0;
    // SHARDED_OPTIMISATION.md S0 — reset Pass A/B/C breakdown for this
    // step. Accumulated across every `commitBuffersSharded` call below.
    commitBreakdown_ = CommitBreakdown{};

    // §3.6 batch 13a: reset commit-hash to FNV-1a-64 offset basis at
    // step start. Every applied command mixes into this in
    // `commitBuffer`; we publish it on `EngineStats::commitHash` at
    // step end.
    commitHashAcc_ = 0xcbf29ce484222325ull;

    // Reset per-system "last step" counters; lifetime totals are preserved.
    // ADAPTIVE_TUNING.md T3 — avgSubJobMicros is intentionally NOT
    // reset (it's an EWMA that should persist across steps), but
    // subJobsLastStep mirrors the other *LastStep counters.
    for (auto& ss : systemStats_) {
        ss.lastUpdateSeconds = 0.0;
        ss.jobsSubmittedLastStep = 0;
        ss.commandsCommittedLastStep = 0;
        ss.waitSeconds = 0.0;
        ss.peakQueueDepth = 0;
        ss.buildRenderFrameSeconds = 0.0;
        ss.subJobsLastStep = 0;
    }

    // §3.1 preStep: serial, registration order, on the sim thread. Hooks
    // run before any wave starts so they can pump per-tick input queues
    // or reset scratch state. Commands emitted via ctx.single() are
    // committed immediately so wave systems observe them.
    // §3.6 batch 13c — rebuild the wave-scoped WorldView before
    // serial preStep hooks. Hooks commit between each call so the
    // view stays consistent with the world they observe.
    worldView_.rebuild(world_);
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        SystemContextImpl ctx(*this, world_, worldView_, dt, tick_,
                              systemPreferredGrain(i),
                              systemPreferredWorkerCap(i));
        systems_[i]->preStep(ctx);
        const auto commitStart = std::chrono::steady_clock::now();
        for (auto& cb : ctx.buffers()) commitBuffer(cb);
        commitSecondsThisStep_ += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - commitStart).count();
        worldView_.rebuild(world_);
    }

    // §3.5 batch 12: reset the budget flag at step start. Workers see
    // false until we observe an over-budget wave boundary below.
    overBudget_.store(false, std::memory_order_release);

    // §3.5 batch 12: helper — decide whether system `sysIdx` is skipped
    // this tick under the active policy. Returns the reason tag if
    // skipped, or empty if not. Per-wave / pre-update.
    auto skipReason = [this](std::size_t sysIdx) -> std::string_view {
        if (!systems_[sysIdx]->skippable()) return {};
        if (skipPolicy_ == SkipPolicy::Budget) {
            if (overBudget_.load(std::memory_order_acquire)) return "budget";
        } else { // Scripted
            const char* nm = systems_[sysIdx]->name();
            if (!nm) return {};
            const std::string_view view(nm);
            for (const auto& s : scriptedSkips_) {
                if (s.tick == tick_ && s.systemName == view) return "scripted";
            }
        }
        return {};
    };

    // Run systems wave by wave. Within a wave, all systems have pairwise
    // non-conflicting declared read/write sets, so they're safe to drive
    // concurrently — each writes into its own SystemContext's buffer list
    // and reads through `const World&`. Across waves we serialize: a later
    // wave's systems see the previous wave's commits.
    for (const auto& wave : waves_) {
        // §3.6 batch 13c — refresh the WorldView at the top of every
        // wave; the previous wave's commits may have changed the
        // chunk inventory. All systems in this wave share the same
        // view (they see pre-wave world state).
        worldView_.rebuild(world_);

        // SHARDED_OPTIMISATION.md S8 — clear the cumulative migrating
        // bitmap at the top of each wave. Buffers in this wave were
        // recorded against post-previous-wave storage; migrations from
        // pre-step or earlier waves are now baked in, so the bitmap
        // starts fresh. commitBuffersSharded accumulates across all
        // systems in this wave so a later system's bucket entries can
        // be demoted if an earlier system migrated their entity.
        for (auto idx : shardMigratingIndices_) shardMigratingBitmap_[idx] = 0;
        shardMigratingIndices_.clear();

        std::vector<std::unique_ptr<SystemContextImpl>> ctxs;
        ctxs.reserve(wave.size());
        for (std::size_t k = 0; k < wave.size(); ++k) {
            ctxs.push_back(std::make_unique<SystemContextImpl>(
                *this, world_, worldView_, dt, tick_,
                systemPreferredGrain(wave[k]),
                systemPreferredWorkerCap(wave[k])));
        }

        // Pre-compute skip decisions for this wave. The result drives
        // both `update()` invocation and stats reporting.
        std::vector<std::string_view> waveSkipReason(wave.size());
        for (std::size_t k = 0; k < wave.size(); ++k) {
            waveSkipReason[k] = skipReason(wave[k]);
        }

        // Per-system update durations, captured by whichever thread runs
        // the system. Slot k corresponds to wave[k].
        std::vector<double> updateSeconds(wave.size(), 0.0);
        auto runIndex = [&](std::size_t k) {
            if (!waveSkipReason[k].empty()) {
                updateSeconds[k] = 0.0;
                return;
            }
            const auto t0 = std::chrono::steady_clock::now();
            systems_[wave[k]]->update(*ctxs[k]);
            updateSeconds[k] = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
        };

        if (wave.size() == 1) {
            runIndex(0);
        } else {
            // §3.10.2 batch 22 — F1 fix. Previously this branch spawned
            // one raw `std::thread` per concurrent system per wave per
            // tick (~240 thread creations/sec at 60 Hz with 4-system
            // waves). Now the wave's parallel system bodies dispatch
            // through the existing `JobSystem` — workers are already
            // parked on a CV, so wakeup is sub-µs instead of multi-ms
            // thread create. The tail still runs on the sim thread to
            // avoid a wasted submit + wait round-trip.
            JobLatch waveDone(static_cast<std::ptrdiff_t>(wave.size() - 1));
            for (std::size_t k = 0; k + 1 < wave.size(); ++k) {
                jobs_->submit([&runIndex, &waveDone, k] {
                    runIndex(k);
                    waveDone.count_down();
                });
            }
            runIndex(wave.size() - 1);
            waveDone.wait();
        }

        // Emit SystemSkipped events for any skipped slot. Drained at the
        // tick boundary like every other typed channel.
        if (publicEngine_) {
            for (std::size_t k = 0; k < wave.size(); ++k) {
                if (waveSkipReason[k].empty()) continue;
                SystemSkipped ev;
                ev.tick       = tick_;
                ev.systemName = systems_[wave[k]]->name()
                              ? std::string_view(systems_[wave[k]]->name())
                              : std::string_view();
                ev.reason     = waveSkipReason[k];
                publicEngine_->events<SystemSkipped>().emit(ev);
            }
        }

        // Commit buffers in registration order (wave[] is already in
        // registration order). Sibling systems wrote to disjoint component
        // categories, so commit order among them is observationally a no-op,
        // but we keep it deterministic to make stats and side-effects stable.
        // We bracket each system's commit with a snapshot of commandsThisStep_
        // so per-system command counts come out of the same accumulator.
        for (std::size_t k = 0; k < wave.size(); ++k) {
            const std::uint64_t commandsBefore = commandsThisStep_;
            const auto commitStart = std::chrono::steady_clock::now();
            // §3.6 batch 13b — sharded commit toggle. Default is the
            // single-threaded reference path; `singleThreadedCommit
            // = false` opts into per-chunk parallel apply for the
            // value-only commands (SetTransform / SetVelocity /
            // SetAcceleration / SetUserData on non-migrating
            // entities). Migrate-possible commands always run on the
            // sim thread to preserve registration-order semantics.
            if (cfg_.singleThreadedCommit) {
                for (auto& cb : ctxs[k]->buffers()) {
                    commitBuffer(cb);
                }
            } else {
                commitBuffersSharded(ctxs[k]->buffers());
            }
            commitSecondsThisStep_ += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - commitStart).count();
            const std::uint64_t cmds = commandsThisStep_ - commandsBefore;
            const std::uint64_t jobs = ctxs[k]->jobsSubmitted();
            jobsThisStep += jobs;

            auto& ss = systemStats_[wave[k]];
            ss.lastUpdateSeconds = updateSeconds[k];
            ss.jobsSubmittedLastStep = jobs;
            ss.commandsCommittedLastStep = cmds;
            ss.totalJobsSubmitted += jobs;
            ss.totalCommandsCommitted += cmds;
            ss.waitSeconds = ctxs[k]->waitSeconds();
            ss.peakQueueDepth = ctxs[k]->peakQueueDepth();
            // EWMA with alpha = 1/16, matching EngineStats::avgStepSeconds.
            // First sample initializes the average.
            constexpr double kEwmaAlpha = 1.0 / 16.0;
            if (stats_.totalTicks == 0) {
                ss.avgUpdateSeconds = ss.lastUpdateSeconds;
            } else {
                ss.avgUpdateSeconds = ss.avgUpdateSeconds * (1.0 - kEwmaAlpha)
                                    + ss.lastUpdateSeconds * kEwmaAlpha;
            }
            // ADAPTIVE_TUNING.md T3 — fold per-sub-job duration EWMA.
            // `jobs` is the number of parallelFor sub-jobs dispatched
            // from this ctx; `subJobNanos` is their summed lambda
            // duration in ns. Divide for the mean, convert to µs,
            // then EWMA-update. We update only when this step had
            // sub-jobs — a step with `jobs == 0` is not a fresh
            // sample and must not pull the average towards zero.
            ss.subJobsLastStep = static_cast<std::uint32_t>(jobs);
            if (jobs > 0) {
                const double meanUs =
                    static_cast<double>(ctxs[k]->subJobNanosTotal())
                    / static_cast<double>(jobs) / 1000.0;
                if (ss.avgSubJobMicros == 0.0) {
                    ss.avgSubJobMicros = meanUs;
                } else {
                    ss.avgSubJobMicros =
                        ss.avgSubJobMicros * (1.0 - kEwmaAlpha)
                        + meanUs * kEwmaAlpha;
                }
            }
        }

        // §3.5 batch 12: post-wave budget check. We sample wall-clock
        // AFTER the commit phase so a wave that overruns hands the next
        // wave a chance to skip its `Skippable` systems. Pure Scripted
        // mode bypasses this — its skip decisions come from the queue,
        // not the clock.
        if (skipPolicy_ == SkipPolicy::Budget && tickBudgetSeconds_ > 0.0) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - stepStart).count();
            if (elapsed > tickBudgetSeconds_) {
                overBudget_.store(true, std::memory_order_release);
            }
        }
    }

    // §3.1 postStep: serial, registration order, after every wave's
    // commit. Use it to publish per-tick events or finalize accumulators.
    // Commands emitted here are visible to the next tick's preStep, not
    // to this tick's wave systems.
    worldView_.rebuild(world_);
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        SystemContextImpl ctx(*this, world_, worldView_, dt, tick_,
                              systemPreferredGrain(i),
                              systemPreferredWorkerCap(i));
        systems_[i]->postStep(ctx);
        const auto commitStart = std::chrono::steady_clock::now();
        for (auto& cb : ctx.buffers()) commitBuffer(cb);
        commitSecondsThisStep_ += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - commitStart).count();
        worldView_.rebuild(world_);
    }

    // §3.3 resource loaders pump after the last postStep commit and
    // before the tick boundary. Loaders are sim-thread-only and own
    // any I/O threads themselves; we just give them their per-tick
    // poll opportunity. Iterating in registration order keeps
    // teardown order well-defined.
    //
    // §3.5 batch 12: call cancel() BEFORE update() so a loader can drop
    // newly-stale requests this same tick rather than waiting another
    // tick. Each loader's `cancel()` and `update()` run on the sim
    // thread back-to-back.
    if (publicEngine_) {
        for (auto& loader : resourceLoaders_) {
            if (!loader) continue;
            loader->cancel(*publicEngine_);
            loader->update(*publicEngine_);
        }
    }

    // §3.5 reaping: any handle that was reserveHandle()-ed but never
    // matched a cb.spawn(handle, ...) is dropped here. Bumps generation
    // so the user's handle stops validating.
    world_.impl_().storage.discardAllReservations();

    // §3.3 drain event channels: writes from this tick flip into the
    // next tick's read buffer so drainTick() sees this tick's events.
    for (auto& [type, entry] : eventChannels_) {
        if (entry.drain) entry.drain(entry.ptr);
    }

    // §3.2 batch 8: render-prep hook. Each system writes its slice into
    // a private RenderFrameBuilder; the engine merges them in
    // registration order during buildRenderFrame() below. Hooks run on
    // the sim thread, single-threaded — every gameplay change has
    // settled by this point.
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        systemRenderBuilders_[i].reset();
        // §3.6.5 batch 15a — time the per-system hook so a slow
        // render-prep step shows up in `SystemStats::buildRenderFrameSeconds`
        // rather than being silently bundled into `lastStepSeconds`.
        const auto brfStart = std::chrono::steady_clock::now();
        systems_[i]->buildRenderFrame(systemRenderBuilders_[i]);
        systemRenderBuilders_[i].finalizeDebugTextViews();
        const auto brfEnd = std::chrono::steady_clock::now();
        if (i < systemStats_.size()) {
            systemStats_[i].buildRenderFrameSeconds =
                std::chrono::duration<double>(brfEnd - brfStart).count();
        }
    }

    tick_++;
    // Wall-clock advances by one fixed step regardless of time scale —
    // only `dt` seen by systems is scaled. Keeps tick_ ↔ simulationTime_
    // a clean integer relationship.
    simulationTime_ += cfg_.fixedStepSeconds;

    const auto brfStart_ = std::chrono::steady_clock::now();
    buildRenderFrame();
    const auto brfMidEngine_ = std::chrono::steady_clock::now();
    if (renderer_) {
        const unsigned front = frontIndex_.load(std::memory_order_acquire);
        renderer_->submitFrame(renderFrames_[front]);
    }

    const auto stepEnd = std::chrono::steady_clock::now();
    const double stepSeconds = std::chrono::duration<double>(stepEnd - stepStart).count();
    // 2026-05-20 — surface render-frame publish vs renderer submit
    // timings. Engine-private fields; readable via stats() if we
    // expose them, but for now just stashed for the next access.
    stats_.engineBuildRenderFrameSeconds =
        std::chrono::duration<double>(brfMidEngine_ - brfStart_).count();
    stats_.renderSubmitSeconds =
        std::chrono::duration<double>(stepEnd - brfMidEngine_).count();

    stats_.tick = tick_;
    stats_.lastStepSeconds = stepSeconds;
    stats_.commitDurationSeconds = commitSecondsThisStep_;
    // EWMA with alpha = 1/16. First sample initializes the average.
    if (stats_.totalTicks == 0) {
        stats_.avgStepSeconds = stepSeconds;
    } else {
        constexpr double kEwmaAlpha = 1.0 / 16.0;
        stats_.avgStepSeconds = stats_.avgStepSeconds * (1.0 - kEwmaAlpha)
                              + stepSeconds * kEwmaAlpha;
    }
    stats_.jobsSubmittedLastStep = jobsThisStep;
    stats_.commandsCommittedLastStep = commandsThisStep_;
    stats_.aliveEntities = world_.size();
    stats_.totalTicks++;
    stats_.totalJobsSubmitted += jobsThisStep;
    stats_.totalCommandsCommitted += commandsThisStep_;
    // §3.6 batch 30 — `legacyCommitHash` (the v1.x path) keeps
    // `commitHashAcc_` as the running per-command FNV-1a-64 mix;
    // publish it directly. Otherwise roll up per-archetype dirty
    // chunks into the new state-fingerprint hash.
    if (cfg_.legacyCommitHash) {
        stats_.commitHash = commitHashAcc_;
    } else {
        stats_.commitHash = finalizeCommitHash();
    }

    // §3.6 batch 13a: opt-in production diagnostic. When
    // `logCommitHashEvery > 0`, log the running hash at Info every N
    // ticks. Devs comparing two clients with the same seed but
    // diverging hashes get a tick number to bisect against. Reads
    // from `stats_.commitHash` (already populated above) so the
    // log line agrees with whatever the rest of the engine reports.
    if (cfg_.logCommitHashEvery > 0 &&
        (stats_.totalTicks % cfg_.logCommitHashEvery) == 0) {
        std::ostringstream os;
        os << "commitHash tick=" << stats_.tick
           << " hash=0x" << std::hex << stats_.commitHash;
        logger().log(LogLevel::Info, os.str());
    }

    // §3.7 batch 14 — sink callback (if any). Snapshot is built from
    // the just-published stats; identical to what `frameSnapshot()`
    // would return. Sim thread; sink is expected to be cheap.
    if (traceSink_) {
        FrameSnapshot snap{
            stats_,
            std::span<const SystemStats>(systemStats_.data(), systemStats_.size()),
            jobs_->stats(),
        };
        traceSink_->onFrame(snap);
    }

    // ADAPTIVE_TUNING.md T4 — adaptive tuner callback. observe() sees
    // the just-published stats; propose() may stage a patch for the
    // NEXT tick (applied at the top of the next step() before
    // preStep, never mid-wave). ADAPTIVE_TUNING.md T6 layered on top:
    //   - Off: skip.
    //   - Active: run observe/propose; record applied patch to trace
    //     if one is attached.
    //   - Scripted: ignore policy.propose entirely; pull next patch
    //     from the attached trace keyed by the current tick.
    if (tuningMode_ == TuningMode::Active && tuningPolicy_) {
        const std::span<const SystemStats> sysSpan(
            systemStats_.data(), systemStats_.size());
        tuningPolicy_->observe(stats_, sysSpan, jobs_->stats());
        auto patch = tuningPolicy_->propose();
        if (patch.has_value() && !patch->grainOverrides.empty()) {
            if (tuningTrace_) {
                tuningTrace_->record(stats_.tick, *patch);
            }
            pendingPatch_ = std::move(patch);
        }
    } else if (tuningMode_ == TuningMode::Scripted && tuningTrace_) {
        TuningPatch scripted;
        if (tuningTrace_->tryGet(stats_.tick, scripted) &&
            !scripted.grainOverrides.empty()) {
            pendingPatch_ = std::move(scripted);
        }
    }

    // §3.7 batch 14 — clear the "stall already emitted" latch so the
    // next tick can fire its own EngineStall event if it also stalls.
    watchdogStallEmitted_.store(false, std::memory_order_release);
}

void EngineImpl::run() {
    if (!initialized_) return;

    lastIterationTime_ = std::chrono::steady_clock::now();
    accumulatedTime_ = 0.0;

    while (!quit_.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - lastIterationTime_).count();
        lastIterationTime_ = now;
        accumulatedTime_ += elapsed;

        std::uint32_t stepsThisIter = 0;
        while (accumulatedTime_ >= cfg_.fixedStepSeconds &&
               stepsThisIter < cfg_.maxStepsPerIteration &&
               !quit_.load(std::memory_order_acquire)) {
            step();
            accumulatedTime_ -= cfg_.fixedStepSeconds;
            stepsThisIter++;
        }

        // If we're falling badly behind, drop accumulated time. Avoids the
        // spiral of death where each iteration is owed more time than the
        // last.
        if (accumulatedTime_ > cfg_.fixedStepSeconds * cfg_.maxStepsPerIteration) {
            accumulatedTime_ = 0.0;
        }

        // Hand the renderer an interpolation frame describing where we are
        // between the last committed tick and the next one. step() already
        // submitted alpha=0 immediately after the sim work; this extra
        // submit reflects wall-clock drift since.
        if (renderer_ && stepsThisIter > 0) {
            const float alpha = static_cast<float>(
                accumulatedTime_ / cfg_.fixedStepSeconds);
            submitInterpolatedFrame(alpha);
        }

        if (cfg_.sleepToPace && !quit_.load(std::memory_order_acquire)) {
            const double remaining = cfg_.fixedStepSeconds - accumulatedTime_;
            if (remaining > 0.0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
            }
        }
    }
}

void EngineImpl::setTimeScale(double scale) noexcept {
    // Clamp to non-negative — a negative scale would mean "go backwards"
    // which is nonsense for a forward-only fixed-step loop.
    timeScale_ = scale < 0.0 ? 0.0 : scale;
}

void EngineImpl::pushScriptedSkip(std::uint64_t tick,
                                  std::string_view systemName) {
    scriptedSkips_.push_back(ScriptedSkip{tick, std::string(systemName)});
}

void EngineImpl::clearScriptedSkips() noexcept {
    scriptedSkips_.clear();
}

void* EngineImpl::getEventChannelRaw(std::type_index type,
                                     void* (*factory)(),
                                     void (*deleter)(void*),
                                     void (*drainFn)(void*)) {
    // Worker jobs and the stall watchdog thread (§3.7 batch 14) both
    // call `Engine::events<T>()`, which routes here. Guard the map
    // mutation under a mutex so a concurrent first-call from a
    // non-sim thread can't race with a sim-thread insertion. Hot
    // path (lookup hit) still pays only the mutex cost — acceptable
    // because per-type channels are constructed once.
    std::lock_guard<std::mutex> lk(eventChannelsMtx_);
    auto it = eventChannels_.find(type);
    if (it != eventChannels_.end()) return it->second.ptr;
    EventChannelEntry entry;
    entry.ptr     = factory();
    entry.deleter = deleter;
    entry.drain   = drainFn;
    void* raw = entry.ptr;
    eventChannels_.emplace(type, entry);
    return raw;
}

void EngineImpl::shutdown() {
    if (!initialized_) return;
    initialized_ = false;

    {
        std::ostringstream os;
        os << "engine shutdown after " << stats_.totalTicks
           << " tick(s), "
           << stats_.totalCommandsCommitted << " command(s) committed";
        logger().log(LogLevel::Info, os.str());
    }

    // §3.9.5 batch 20 — drain any pending async snapshot callbacks
    // before tearing down storage. The worker is joined here; any
    // queued callbacks run synchronously on the sim thread.
    stopSnapshotWorker_();

    // Drain any in-flight jobs before we start tearing things down.
    if (jobs_) jobs_->waitIdle();

    if (game_ && publicEngine_) {
        game_->onTeardown(*publicEngine_, world_);
    }

    if (renderer_) {
        renderer_->shutdown();
    }

    for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
        (*it)->onUnregister(world_);
    }
    systems_.clear();

    // §3.2 batch 7: notify loaders so they can cancel in-flight work
    // before destruction. Reverse-registration order matches the
    // teardown that immediately follows so a loader's onShutdown sees
    // the same engine state its destructor will.
    if (publicEngine_) {
        for (auto it = resourceLoaders_.rbegin();
             it != resourceLoaders_.rend(); ++it) {
            if (*it) (*it)->onShutdown(*publicEngine_);
        }
    }

    // Release resource loaders in reverse-registration order, so a
    // loader that depends on an earlier-registered one tears down
    // first.
    while (!resourceLoaders_.empty()) {
        resourceLoaders_.pop_back();
    }
}

} // namespace threadmaxx::internal
