#pragma once

/// @file panels/menu_bar.hpp
/// @brief `MenuBar` — top-edge menu chrome. The host registers named
/// actions (e.g. `File → New Panel`) and the test / hotkey path
/// triggers them by name.

#include "../panel.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx::studio {

class MenuBar : public IStudioPanel {
public:
    /// @brief One registered menu action.
    struct Action {
        std::string menu;
        std::string label;
        std::function<void()> callback;
    };

    std::string_view id() const noexcept override {
        return "studio.menu_bar";
    }
    std::string_view title() const noexcept override { return "MenuBar"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Register an action under a top-level menu. Identical
    /// `(menu, label)` overwrites the existing callback.
    void addAction(std::string_view menu, std::string_view label,
                   std::function<void()> callback);

    /// @brief Programmatic / hotkey-driven action dispatch. Returns
    /// `true` when an action matched and its callback was invoked.
    bool trigger(std::string_view menu, std::string_view label);

    /// @brief Total registered actions.
    std::size_t actionCount() const noexcept { return actions_.size(); }

private:
    std::vector<Action> actions_;
};

} // namespace threadmaxx::studio
