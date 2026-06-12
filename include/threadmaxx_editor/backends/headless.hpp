#pragma once

/// @file backends/headless.hpp
/// @brief Deterministic capture backend used by the editor test suite.
///
/// Every backend call is appended to `capturedFrame()`, in submission
/// order. Real-UI backends do not derive from this — tests assert on
/// the captured ops directly.

#include "../backend.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace threadmaxx::editor {

/// @brief One captured draw op. Tag dispatches on `op`.
struct CapturedOp {
    enum class Op : std::uint8_t {
        BeginFrame,
        EndFrame,
        DrawText,
        DrawRect,
    };

    Op op{Op::BeginFrame};
    std::string text;
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};
};

/// @brief Borrowed view of the headless backend's captured ops in
/// submission order. Resets each `beginFrame()`.
struct CapturedFrame {
    std::vector<CapturedOp> ops;

    void clear() noexcept { ops.clear(); }
    std::size_t size() const noexcept { return ops.size(); }
    bool empty() const noexcept { return ops.empty(); }
};

class HeadlessBackend final : public IEditorBackend {
public:
    HeadlessBackend() = default;

    bool initialize() override;
    void shutdown() override;
    void beginFrame() override;
    void endFrame() override;
    void drawText(std::string_view text, float x, float y) override;
    void drawRect(float x, float y, float w, float h) override;

    /// @brief Captured ops from the most recent (or in-progress) frame.
    /// Cleared by every `beginFrame()`.
    const CapturedFrame& capturedFrame() const noexcept { return frame_; }

    /// @brief True between `beginFrame()` and `endFrame()`.
    bool inFrame() const noexcept { return inFrame_; }

    /// @brief True after `initialize()` and before `shutdown()`.
    bool initialized() const noexcept { return initialized_; }

private:
    CapturedFrame frame_{};
    bool initialized_{false};
    bool inFrame_{false};
};

} // namespace threadmaxx::editor
