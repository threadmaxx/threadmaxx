#pragma once

/// @file replay.hpp
/// @brief E15 — capture/replay surface over `WorldSnapshot` streams.
///
/// `CaptureStream` is an append-only buffer of `(tick, WorldSnapshot)`
/// frames with its own wire format on top of the engine's existing
/// `WorldSnapshot` serializer. `ReplaySession` exposes a cursor over
/// a `CaptureStream` and a snapshot-driven `listEntities()` view so
/// the editor (and the studio panel built on it) can drive panels
/// off saved data instead of a live `Engine`.
///
/// E15 is *not* a parallel `Engine` API. It deliberately stops at
/// "what's the current frame and what entities does it carry."
/// Resources / systems / job stats remain a live-only surface.

#include <cstdint>
#include <iosfwd>
#include <vector>

#include <threadmaxx/Serialization.hpp>

#include "inspect.hpp"

namespace threadmaxx::editor {

/// @brief One captured frame.
struct CaptureFrame {
    std::uint64_t               tick{0};
    threadmaxx::WorldSnapshot   snapshot;
};

/// @brief Wire-format magic / version for `CaptureStream`. Bump
/// `kCaptureStreamVersion` on any layout change. The per-frame
/// `WorldSnapshot` blob carries its own magic / version — a capture
/// file is therefore self-describing at both layers.
inline constexpr std::uint32_t kCaptureStreamMagic   = 0x54504143u; // 'CAPT' (LE)
inline constexpr std::uint32_t kCaptureStreamVersion = 1u;

/// @brief Append-only buffer of captured frames + binary serializer.
class CaptureStream {
public:
    CaptureStream() = default;

    /// @brief Append a frame. The snapshot is moved into the buffer.
    void append(std::uint64_t tick, threadmaxx::WorldSnapshot snapshot);

    [[nodiscard]] std::size_t frameCount() const noexcept {
        return frames_.size();
    }
    [[nodiscard]] bool empty() const noexcept { return frames_.empty(); }

    /// @brief Borrowed access to the frame at @p index. The reference
    /// is invalidated by any subsequent `append` / `clear` / `load`.
    [[nodiscard]] const CaptureFrame& frame(std::size_t index) const {
        return frames_.at(index);
    }
    [[nodiscard]] std::span<const CaptureFrame> frames() const noexcept {
        return std::span<const CaptureFrame>(frames_.data(), frames_.size());
    }

    void clear() noexcept { frames_.clear(); }

    /// @brief Write every captured frame as a self-describing binary
    /// blob: `[magic 'CAPT'][version u32][count u64]
    ///        [(tick u64, WorldSnapshot blob) * count]`.
    void save(std::ostream& out) const;

    /// @brief Read a stream produced by @ref save. Returns false on
    /// any magic / version / payload mismatch; the stream is replaced
    /// only on success.
    [[nodiscard]] bool load(std::istream& in);

private:
    std::vector<CaptureFrame> frames_;
};

/// @brief Cursor over a `CaptureStream`. The session does not own
/// the stream — pass a stream that outlives the session.
///
/// `current()` is the snapshot at the cursor; `seek` / `step` move
/// the cursor; `listEntities` produces an entity summary from the
/// snapshot at the cursor.
///
/// @thread_safety Single-threaded by convention. Capture playback is
/// editor-side bookkeeping; the engine is not involved.
class ReplaySession {
public:
    explicit ReplaySession(const CaptureStream& stream) noexcept;

    [[nodiscard]] std::size_t frameCount() const noexcept {
        return stream_->frameCount();
    }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

    /// @brief Snapshot at the cursor, or `nullptr` if the stream is
    /// empty.
    [[nodiscard]] const threadmaxx::WorldSnapshot* current() const noexcept;

    /// @brief Tick recorded with the current frame, or 0 when empty.
    [[nodiscard]] std::uint64_t currentTick() const noexcept;

    /// @brief Move the cursor. Out-of-range inputs clamp to the
    /// nearest valid index; on an empty stream the cursor stays at 0.
    void seek(std::size_t index) noexcept;

    /// @brief Advance the cursor by @p delta frames (can be negative).
    /// Clamps to valid range; never wraps.
    void step(std::int64_t delta) noexcept;

    /// @brief Entity rows derived from the current snapshot's
    /// `entities` + `masks` arrays. Mirrors `Inspector::listEntities`
    /// so studio's replay panel can share UI rendering with the live
    /// panel.
    [[nodiscard]] std::vector<EntitySummary> listEntities() const;

private:
    const CaptureStream* stream_;
    std::size_t          cursor_{0};
};

} // namespace threadmaxx::editor
