#pragma once

/// @file backends/remote.hpp
/// @brief E14 — `RemoteBackend` serializes editor draw calls into a
/// host-endian byte stream; `decodeRemoteStream` replays the stream
/// back into a real backend on the receiving side.
///
/// This is the wire backbone for studio's M7 `RemoteDataSource`: the
/// in-process editor records into a `RemoteBackend`, the buffered
/// bytes ship over a transport (sockets / files / IPC — chosen at
/// the studio layer), and the receiver decodes into any
/// `IEditorBackend` (typically `HeadlessBackend` for tests; an
/// ImGui-bound backend for the studio shell).
///
/// Wire layout (host-endian, no cross-machine portability promised):
///
///     stream      := frame*
///     frame       := op-record+
///     op-record   := tag:u8 payload
///     tag values  := 'B'(0x01) BeginFrame | 'E'(0x02) EndFrame
///                 |  'T'(0x03) DrawText   | 'R'(0x04) DrawRect
///     BeginFrame  : no payload
///     EndFrame    : no payload
///     DrawText    : x:f32 y:f32 len:u32 utf8-bytes
///     DrawRect    : x:f32 y:f32 w:f32 h:f32
///
/// Bumping a tag value or the layout of any payload is a wire-format
/// break — pair with a `kRemoteWireVersion` constant bump in callers
/// that care about cross-version readers.

#include "../backend.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace threadmaxx::editor {

/// @brief Wire-format version constant. Callers that store / cache
/// recorded bytes across releases should record this alongside the
/// payload so a future decoder can detect a format break.
inline constexpr std::uint32_t kRemoteWireVersion = 1;

/// @brief Tag values for `RemoteBackend` op records. Public so the
/// decoder can be re-implemented (e.g. in a Rust / Go studio host)
/// against the same enumeration.
enum class RemoteOpTag : std::uint8_t {
    BeginFrame = 0x01,
    EndFrame   = 0x02,
    DrawText   = 0x03,
    DrawRect   = 0x04,
};

/// @brief `IEditorBackend` that records every call into a byte
/// buffer. `initialize()` and `shutdown()` are no-ops (the buffer
/// is the only state).
///
/// @thread_safety Single-threaded by convention (mirrors the rest
///                of `IEditorBackend`).
class RemoteBackend final : public IEditorBackend {
public:
    RemoteBackend() = default;

    bool initialize() override { initialized_ = true; return true; }
    void shutdown()   override { initialized_ = false; }
    void beginFrame() override;
    void endFrame()   override;
    void drawText(std::string_view text, float x, float y) override;
    void drawRect(float x, float y, float w, float h) override;

    /// @brief Bytes recorded since the last `clear()`.
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::span<const std::byte>(buffer_.data(), buffer_.size());
    }

    /// @brief Reset the buffer. Does NOT touch `initialized()`.
    void clear() noexcept { buffer_.clear(); }

    /// @brief True between `initialize()` and `shutdown()`.
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    std::vector<std::byte> buffer_;
    bool initialized_{false};
};

/// @brief Replay an encoded byte stream into @p sink. Returns the
/// number of bytes consumed on success; returns 0 on a truncated
/// stream or an unknown op tag. The sink must be initialized;
/// `beginFrame` / `endFrame` calls in the stream are forwarded
/// verbatim.
///
/// `decodeRemoteStream` does NOT call `sink.initialize()` /
/// `sink.shutdown()` — those are lifecycle calls the host owns.
[[nodiscard]] std::size_t
decodeRemoteStream(std::span<const std::byte> bytes,
                   IEditorBackend& sink);

} // namespace threadmaxx::editor
