/// @file ResourceRegistry.cpp
/// Type-erased implementation of the public @ref threadmaxx::ResourceRegistry.
///
/// Each `std::type_index` gets its own bucket of slots. Slots are reused
/// after `remove()` (or after the last refcount drops); generations bump
/// so stale handles never alias new values. All operations take a single
/// internal mutex — that's fine for setup-time registration and per-frame
/// lookups but not for high-throughput concurrent inserts.
///
/// §3.2 batch 7 added an opt-in refcount path. Slot::refCount tracks
/// outstanding ResourceHandle copies; addRefCounted seeds it to 1 and
/// release decrements. When refCount hits zero on a refcount-managed slot
/// (Slot::refCounted == true), the slot is freed automatically. Slots
/// added via the legacy `add(...)` path keep `refCounted == false` and
/// are only freed by an explicit `remove`.
#include "threadmaxx/Resource.hpp"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace threadmaxx {

struct ResourceRegistry::Impl {
    struct Slot {
        std::shared_ptr<void> value;
        std::uint32_t generation = 0;  // 0 means "never used"
        std::uint32_t refCount   = 0;  // only meaningful when refCounted
        bool          alive      = false;
        bool          refCounted = false;
    };
    struct Bucket {
        std::vector<Slot>         slots;
        std::vector<std::uint32_t> freeSlots;
        std::size_t aliveCount = 0;
    };
    mutable std::mutex mtx;
    std::unordered_map<std::type_index, Bucket> buckets;

    // Caller must hold mtx. Frees the slot, bumps generation, returns
    // it to the free list. Used by removeRaw_ and the refcount drop path.
    void freeSlotLocked_(Bucket& bucket, std::uint32_t idx) {
        auto& slot = bucket.slots[idx];
        slot.alive      = false;
        slot.refCounted = false;
        slot.refCount   = 0;
        slot.value.reset();
        bucket.freeSlots.push_back(idx);
        bucket.aliveCount--;
    }
};

ResourceRegistry::ResourceRegistry() : impl_(std::make_unique<Impl>()) {}
ResourceRegistry::~ResourceRegistry() = default;

std::pair<std::uint32_t, std::uint32_t>
ResourceRegistry::addRaw_(std::type_index type, std::shared_ptr<void> value) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    auto& bucket = impl_->buckets[type];

    std::uint32_t idx;
    if (!bucket.freeSlots.empty()) {
        idx = bucket.freeSlots.back();
        bucket.freeSlots.pop_back();
    } else {
        idx = static_cast<std::uint32_t>(bucket.slots.size());
        bucket.slots.emplace_back();
    }

    auto& slot = bucket.slots[idx];
    slot.generation = (slot.generation == 0) ? 1u : slot.generation + 1u;
    slot.alive = true;
    slot.refCounted = false;
    slot.refCount = 0;
    slot.value = std::move(value);
    bucket.aliveCount++;
    return {idx, slot.generation};
}

std::pair<std::uint32_t, std::uint32_t>
ResourceRegistry::addRefCountedRaw_(std::type_index type,
                                    std::shared_ptr<void> value) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    auto& bucket = impl_->buckets[type];

    std::uint32_t idx;
    if (!bucket.freeSlots.empty()) {
        idx = bucket.freeSlots.back();
        bucket.freeSlots.pop_back();
    } else {
        idx = static_cast<std::uint32_t>(bucket.slots.size());
        bucket.slots.emplace_back();
    }

    auto& slot = bucket.slots[idx];
    slot.generation = (slot.generation == 0) ? 1u : slot.generation + 1u;
    slot.alive      = true;
    slot.refCounted = true;
    slot.refCount   = 1;
    slot.value      = std::move(value);
    bucket.aliveCount++;
    return {idx, slot.generation};
}

const void* ResourceRegistry::getRaw_(std::type_index type,
                                      std::uint32_t index,
                                      std::uint32_t generation) const noexcept {
    if (generation == 0) return nullptr;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    const auto it = impl_->buckets.find(type);
    if (it == impl_->buckets.end()) return nullptr;
    const auto& bucket = it->second;
    if (index >= bucket.slots.size()) return nullptr;
    const auto& slot = bucket.slots[index];
    if (!slot.alive || slot.generation != generation) return nullptr;
    return slot.value.get();
}

bool ResourceRegistry::removeRaw_(std::type_index type,
                                  std::uint32_t index,
                                  std::uint32_t generation) noexcept {
    if (generation == 0) return false;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    const auto it = impl_->buckets.find(type);
    if (it == impl_->buckets.end()) return false;
    auto& bucket = it->second;
    if (index >= bucket.slots.size()) return false;
    auto& slot = bucket.slots[index];
    if (!slot.alive || slot.generation != generation) return false;

    impl_->freeSlotLocked_(bucket, index);
    return true;
}

bool ResourceRegistry::retainRaw_(std::type_index type,
                                  std::uint32_t index,
                                  std::uint32_t generation) noexcept {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    const auto it = impl_->buckets.find(type);
    if (it == impl_->buckets.end()) return false;
    auto& bucket = it->second;
    if (index >= bucket.slots.size()) return false;
    auto& slot = bucket.slots[index];
    if (!slot.alive || !slot.refCounted || slot.generation != generation)
        return false;
    slot.refCount++;
    return true;
}

void ResourceRegistry::releaseRaw_(std::type_index type,
                                   std::uint32_t index,
                                   std::uint32_t generation) noexcept {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    const auto it = impl_->buckets.find(type);
    if (it == impl_->buckets.end()) return;
    auto& bucket = it->second;
    if (index >= bucket.slots.size()) return;
    auto& slot = bucket.slots[index];
    if (!slot.alive || !slot.refCounted || slot.generation != generation)
        return;
    if (slot.refCount == 0) return; // defensive; should never hit
    slot.refCount--;
    if (slot.refCount == 0) {
        impl_->freeSlotLocked_(bucket, index);
    }
}

std::uint32_t ResourceRegistry::refCountRaw_(std::type_index type,
                                             std::uint32_t index,
                                             std::uint32_t generation) const noexcept {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    const auto it = impl_->buckets.find(type);
    if (it == impl_->buckets.end()) return 0;
    const auto& bucket = it->second;
    if (index >= bucket.slots.size()) return 0;
    const auto& slot = bucket.slots[index];
    if (!slot.alive || !slot.refCounted || slot.generation != generation)
        return 0;
    return slot.refCount;
}

std::size_t ResourceRegistry::countRaw_(std::type_index type) const noexcept {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    const auto it = impl_->buckets.find(type);
    return it == impl_->buckets.end() ? 0u : it->second.aliveCount;
}

} // namespace threadmaxx
