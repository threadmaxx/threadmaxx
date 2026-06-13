#pragma once

/// @file panels/input.hpp
/// @brief ST17 — `InputPanel` reads an `input::InputContext`'s live
/// state and (optionally) drives an `input::InputTrace`. No sibling
/// prep needed — input v1.0's public surface is sufficient.

#include "../panel.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace threadmaxx::input {
class InputContext;
class InputTrace;
class NullBackend;
} // namespace threadmaxx::input

namespace threadmaxx::studio {

class InputPanel : public IStudioPanel {
public:
    InputPanel() noexcept = default;
    explicit InputPanel(const input::InputContext& context) noexcept;

    void setContext(const input::InputContext* ctx) noexcept { context_ = ctx; }
    [[nodiscard]] const input::InputContext* context() const noexcept {
        return context_;
    }

    /// @brief Bind / unbind an optional trace. When bound, the panel
    /// surfaces `frameCount()` and exposes `recordCurrentFrame` /
    /// `replayFrame` as panel methods the host's button bindings can
    /// hook into.
    void setTrace(input::InputTrace* trace) noexcept { trace_ = trace; }
    [[nodiscard]] input::InputTrace* trace() const noexcept { return trace_; }

    /// @brief Bind / unbind a replay target — `replayFrame` needs a
    /// `NullBackend` to push into.
    void setReplayBackend(input::NullBackend* backend) noexcept {
        replayBackend_ = backend;
    }

    /// @brief Record this frame's context events into the bound
    /// trace. Returns false if either the context or the trace is
    /// unbound.
    bool recordCurrentFrame();

    /// @brief Replay a recorded frame onto the bound `NullBackend`.
    /// Returns false if the trace or backend is unbound, or if the
    /// frame index is out of range.
    bool replayFrame(std::uint64_t frameIndex);

    std::string_view id() const noexcept override { return "sibling.input"; }
    std::string_view title() const noexcept override { return "Input"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Row count emitted by the most recent render.
    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }

private:
    const input::InputContext* context_{nullptr};
    input::InputTrace*         trace_{nullptr};
    input::NullBackend*        replayBackend_{nullptr};
    std::size_t                lastRows_{0};
};

} // namespace threadmaxx::studio
