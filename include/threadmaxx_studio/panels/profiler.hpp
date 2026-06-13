#pragma once

/// @file panels/profiler.hpp
/// @brief ST11 — `ProfilerPanel` wraps editor E13's `ProfilerView`
/// as an `IStudioPanel`. Doubles as `ITraceSink` so a host can wire
/// it straight into `Engine::setTraceSink` and have the panel see
/// every committed frame.

#include "../panel.hpp"

#include <threadmaxx/Telemetry.hpp>
#include <threadmaxx_editor/profiler.hpp>

#include <cstddef>
#include <string_view>

namespace threadmaxx::studio {

class ProfilerPanel : public IStudioPanel,
                     public threadmaxx::ITraceSink {
public:
    /// @brief Construct with a ring capacity (passed straight to
    /// `editor::ProfilerView`). Default 256 frames.
    explicit ProfilerPanel(std::size_t capacity = 256);

    // ITraceSink — called once per Engine::step on the sim thread.
    void onFrame(const threadmaxx::FrameSnapshot& snap) override;

    // IStudioPanel.
    std::string_view id() const noexcept override {
        return "engine.profiler";
    }
    std::string_view title() const noexcept override { return "Profiler"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Borrowed access to the wrapped view (tests inspect it
    /// directly; production code uses `summary()` below).
    [[nodiscard]] const editor::ProfilerView& view() const noexcept {
        return view_;
    }
    [[nodiscard]] editor::ProfilerView& view() noexcept { return view_; }

    /// @brief Convenience pass-through.
    [[nodiscard]] editor::ProfilerSummary summary() const {
        return view_.summary();
    }

    /// @brief Max rows shown in `render()` (panels with more rows
    /// truncate at this cap). Adjustable so a tall panel can show
    /// every system; default 8 fits a compact HUD.
    void setMaxRows(std::size_t n) noexcept { maxRows_ = n; }
    [[nodiscard]] std::size_t maxRows() const noexcept { return maxRows_; }

private:
    editor::ProfilerView view_;
    std::size_t          maxRows_{8};
};

} // namespace threadmaxx::studio
