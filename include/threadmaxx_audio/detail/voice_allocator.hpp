#pragma once

/// @file voice_allocator.hpp
/// @brief Fixed-capacity voice pool with steal-oldest overflow policy.
///
/// Slot layout is contiguous (`std::vector<VoiceSlot>`); allocation is a
/// linear scan for `alive == false`, falling through to the lowest-tick slot
/// when the pool is full. Generation guards `VoiceId` reuse so a stale handle
/// from a stolen / stopped voice cannot accidentally address the slot's new
/// occupant.

#include "threadmaxx_audio/types.hpp"

#include <cstdint>
#include <vector>

namespace threadmaxx::audio::detail {

/// Per-slot voice state. Mixer fields (sound/bus/gain) plus allocator
/// bookkeeping (alive/generation/startTick) plus playback cursor.
struct VoiceSlot {
    bool          alive          = false;
    std::uint32_t generation     = 0;
    std::uint64_t startTick      = 0;
    std::uint64_t playheadFrames = 0;

    SoundId       sound{};
    StreamId      stream{};      ///< AU3 — non-zero means this is a stream voice.
    BusId         bus{};
    float         gainDb         = 0.0f;
    bool          looping        = false;
    bool          isStream       = false;
};

class VoiceAllocator {
public:
    void initialize(std::uint32_t capacity);
    void shutdown() noexcept;

    /// Allocate a free slot, or steal the lowest-`startTick` slot if full.
    /// `stolen` is set true on the steal path. Bumps the slot's generation
    /// so any prior `VoiceId` pointing here decodes as stale.
    [[nodiscard]] std::uint32_t allocate(std::uint64_t currentTick, bool& stolen) noexcept;

    /// Release a slot. Bumps generation so the freed `VoiceId` cannot reach
    /// a future occupant.
    void free(std::uint32_t slotIndex) noexcept;

    [[nodiscard]] std::uint32_t capacity() const noexcept {
        return static_cast<std::uint32_t>(slots_.size());
    }

    [[nodiscard]] std::uint32_t activeCount() const noexcept;

    [[nodiscard]] VoiceSlot&       slot(std::uint32_t i) noexcept       { return slots_[i]; }
    [[nodiscard]] const VoiceSlot& slot(std::uint32_t i) const noexcept { return slots_[i]; }

    /// Encode a slot index as a `VoiceId`. Low 32 bits = slot, high 32 bits
    /// = generation. `VoiceId{0}` is reserved (decodes as invalid).
    [[nodiscard]] VoiceId encode(std::uint32_t slotIndex) const noexcept;

    /// Decode a `VoiceId` back to its slot. Returns false if the handle is
    /// stale (generation mismatch), out of range, or for a dead slot.
    [[nodiscard]] bool decode(VoiceId id, std::uint32_t& slotIndex) const noexcept;

private:
    std::vector<VoiceSlot> slots_;
};

} // namespace threadmaxx::audio::detail
