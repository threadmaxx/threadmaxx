#pragma once

/// @file panels/tuning.hpp
/// @brief `TuningPanel` — ITuningPolicy / TuningTrace inspector +
/// apply-patch button. Talks to the engine's adaptive tuning surface
/// (see ADAPTIVE_TUNING.md / `threadmaxx/Tuning.hpp`).

#include "../panel.hpp"

#include <threadmaxx/Tuning.hpp>

#include <cstddef>
#include <string_view>

namespace threadmaxx {
class Engine;
} // namespace threadmaxx

namespace threadmaxx::studio {

class TuningPanel : public IStudioPanel {
public:
    explicit TuningPanel(threadmaxx::Engine& engine) noexcept
        : engine_(&engine) {}

    std::string_view id() const noexcept override { return "engine.tuning"; }
    std::string_view title() const noexcept override { return "Tuning"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Apply-patch button. Records the patch onto the engine's
    /// installed `TuningTrace` keyed by `tick`, ready for the engine's
    /// Scripted mode (or for tests that want to inspect the trace
    /// without going through a policy). Returns `false` if no trace
    /// is installed.
    bool applyPatch(std::uint64_t tick, const threadmaxx::TuningPatch& patch);

    /// @brief Number of entries in the engine's installed trace (0 if
    /// no trace).
    std::size_t traceSize() const noexcept;

private:
    threadmaxx::Engine* engine_;
};

} // namespace threadmaxx::studio
