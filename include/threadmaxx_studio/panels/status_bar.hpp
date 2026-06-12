#pragma once

/// @file panels/status_bar.hpp
/// @brief `StatusBar` — bottom-edge status chrome. Reports engine FPS
/// and paused state via the data source's `engineSnapshot()`. The
/// data source is the single source of truth so the same bar works
/// in-process and over the remote wire.

#include "../panel.hpp"

#include <string>
#include <string_view>

namespace threadmaxx::studio {

class StatusBar : public IStudioPanel {
public:
    std::string_view id() const noexcept override {
        return "studio.status_bar";
    }
    std::string_view title() const noexcept override { return "StatusBar"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief The text the bar last drew. Useful for tests.
    std::string_view lastStatus() const noexcept { return lastStatus_; }

private:
    std::string lastStatus_;
};

} // namespace threadmaxx::studio
