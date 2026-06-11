#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "threadmaxx_input/events.hpp"

namespace threadmaxx::input {

class InputContext;
class NullBackend;

// Frame-indexed log of raw InputEvents. Record one frame's events with
// `record(ctx)` after `beginFrame` (or during the host poll, whichever is
// cleaner for the host). Drive a fresh context by stuffing recorded
// frames into a NullBackend via `replay(backend, frameIndex)`.
//
// Wire format (see DESIGN_NOTES § 5.6):
//   [magic 'TMIN' u32][version u32][frameCount u64]
//   per frame: [eventCount u32][events...]
//
// Host-endian POD — intended for short-lived test fixtures and in-session
// repro, not long-term archival. Same caveat as WorldSnapshot.
class InputTrace {
public:
    static constexpr std::uint32_t kSerializeMagic = 0x4E494D54u;  // 'TMIN' LE
    static constexpr std::uint32_t kSerializeVersion = 1u;

    // Recording — appends the events the context drained during the
    // most-recent beginFrame call as a new frame entry.
    void recordCurrentFrame(const InputContext& ctx);

    // Manual recording — for hosts that buffer events outside the context.
    void appendFrame(std::span<const InputEvent> events);

    // Replay — pushes the recorded events for `frameIndex` onto `backend`'s
    // queue. The next `beginFrame` call on a context wired to that backend
    // drains them. Returns false if `frameIndex` is out of range.
    bool replayTo(NullBackend& backend, std::uint64_t frameIndex) const;

    std::uint64_t frameCount() const noexcept { return frames_.size(); }
    std::span<const InputEvent> frame(std::uint64_t frameIndex) const noexcept;

    void clear() noexcept;
    void reserve(std::size_t frames);

    std::vector<std::byte> serialize() const;
    bool deserialize(std::span<const std::byte> bytes);

private:
    struct Frame {
        std::vector<InputEvent> events;
    };
    std::vector<Frame> frames_;
};

}  // namespace threadmaxx::input
