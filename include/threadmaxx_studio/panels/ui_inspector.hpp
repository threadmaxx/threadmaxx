#pragma once

/// @file panels/ui_inspector.hpp
/// @brief ST19 — `UIInspectorPanel` peeks into a `ui::UIContext`'s
/// committed `DrawList` (post-`endFrame`) and renders a per-kind
/// command tally.
///
/// The UI v1.x `ContextSnapshot` accessor (DESIGN_NOTES § 7.4 Tier
/// 4) is deferred. When the panel has no `UIContext` bound it
/// renders the "available in v1.x" placeholder per the M4 plan.

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::ui {
class UIContext;
} // namespace threadmaxx::ui

namespace threadmaxx::studio {

class UIInspectorPanel : public IStudioPanel {
public:
    UIInspectorPanel() noexcept = default;
    explicit UIInspectorPanel(const ui::UIContext& context) noexcept;

    void setContext(const ui::UIContext* ctx) noexcept { context_ = ctx; }
    [[nodiscard]] const ui::UIContext* context() const noexcept {
        return context_;
    }

    std::string_view id() const noexcept override {
        return "sibling.ui_inspector";
    }
    std::string_view title() const noexcept override { return "UI Inspector"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Total draw-command count from the most recent render
    /// (0 when no context is bound).
    [[nodiscard]] std::size_t lastDrawCount() const noexcept { return lastDrawCount_; }

    /// @brief True iff the last render produced the v1.x placeholder
    /// (no context bound).
    [[nodiscard]] bool lastWasStub() const noexcept { return lastWasStub_; }

private:
    const ui::UIContext* context_{nullptr};
    std::size_t          lastDrawCount_{0};
    bool                 lastWasStub_{true};
};

} // namespace threadmaxx::studio
