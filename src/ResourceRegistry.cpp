#include "threadmaxx/Resource.hpp"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace threadmaxx {

struct ResourceRegistry::Impl {
    struct Slot {
        std::shared_ptr<void> value;
        std::uint32_t generation = 0;  // 0 means "never used"
        bool          alive      = false;
    };
    struct Bucket {
        std::vector<Slot>         slots;
        std::vector<std::uint32_t> freeSlots;
        std::size_t aliveCount = 0;
    };
    mutable std::mutex mtx;
    std::unordered_map<std::type_index, Bucket> buckets;
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
    slot.value = std::move(value);
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

    slot.alive = false;
    slot.value.reset();
    bucket.freeSlots.push_back(index);
    bucket.aliveCount--;
    return true;
}

std::size_t ResourceRegistry::countRaw_(std::type_index type) const noexcept {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    const auto it = impl_->buckets.find(type);
    return it == impl_->buckets.end() ? 0u : it->second.aliveCount;
}

} // namespace threadmaxx
