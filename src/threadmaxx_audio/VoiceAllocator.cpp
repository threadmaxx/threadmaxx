/// @file VoiceAllocator.cpp
/// @brief AU2 voice pool — fixed capacity, linear free-slot scan, steal the
/// lowest-`startTick` slot on overflow.

#include "threadmaxx_audio/detail/voice_allocator.hpp"

#include <cstdint>

namespace threadmaxx::audio::detail {

void VoiceAllocator::initialize(std::uint32_t capacity) {
    slots_.assign(capacity, VoiceSlot{});
}

void VoiceAllocator::shutdown() noexcept {
    slots_.clear();
}

std::uint32_t VoiceAllocator::activeCount() const noexcept {
    std::uint32_t n = 0;
    for (const auto& s : slots_) {
        if (s.alive) ++n;
    }
    return n;
}

std::uint32_t VoiceAllocator::allocate(std::uint64_t currentTick, bool& stolen) noexcept {
    stolen = false;
    for (std::uint32_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].alive) {
            ++slots_[i].generation;
            slots_[i].alive          = true;
            slots_[i].startTick      = currentTick;
            slots_[i].playheadFrames = 0;
            return i;
        }
    }
    // Pool full: steal the lowest-tick slot.
    std::uint32_t oldest     = 0;
    std::uint64_t oldestTick = slots_[0].startTick;
    for (std::uint32_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].startTick < oldestTick) {
            oldest     = i;
            oldestTick = slots_[i].startTick;
        }
    }
    stolen = true;
    ++slots_[oldest].generation;
    slots_[oldest].alive          = true;
    slots_[oldest].startTick      = currentTick;
    slots_[oldest].playheadFrames = 0;
    return oldest;
}

void VoiceAllocator::free(std::uint32_t slotIndex) noexcept {
    if (slotIndex >= slots_.size()) return;
    slots_[slotIndex].alive = false;
    ++slots_[slotIndex].generation;
}

VoiceId VoiceAllocator::encode(std::uint32_t slotIndex) const noexcept {
    if (slotIndex >= slots_.size()) return VoiceId{0};
    const std::uint64_t gen = static_cast<std::uint64_t>(slots_[slotIndex].generation);
    return VoiceId{ static_cast<std::uint64_t>(slotIndex) | (gen << 32) };
}

bool VoiceAllocator::decode(VoiceId id, std::uint32_t& slotIndex) const noexcept {
    if (id.value == 0) return false;
    const std::uint32_t slot = static_cast<std::uint32_t>(id.value & 0xFFFFFFFFu);
    const std::uint32_t gen  = static_cast<std::uint32_t>(id.value >> 32);
    if (slot >= slots_.size()) return false;
    if (!slots_[slot].alive) return false;
    if (slots_[slot].generation != gen) return false;
    slotIndex = slot;
    return true;
}

} // namespace threadmaxx::audio::detail
