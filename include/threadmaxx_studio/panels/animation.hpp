#pragma once

/// @file panels/animation.hpp
/// @brief ST15 — `AnimationPanel` reads an `animation::Animator`'s
/// diagnostics surface (A9) and renders one row per stats field.
///
/// The host owns the `Animator`; the panel borrows a pointer. The
/// public header forward-declares the animation types so studio
/// shells that don't pull animation headers compile cleanly — only
/// `AnimationPanel.cpp` includes the diagnostics header.

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::animation {
class Animator;
} // namespace threadmaxx::animation

namespace threadmaxx::studio {

class AnimationPanel : public IStudioPanel {
public:
    /// @brief Construct without a bound animator (renders a
    /// "detached" placeholder until @ref setAnimator is called).
    AnimationPanel() noexcept = default;
    explicit AnimationPanel(const animation::Animator& animator) noexcept;

    /// @brief Rebind to a new animator (or pass nullptr to detach).
    void setAnimator(const animation::Animator* animator) noexcept {
        animator_ = animator;
    }
    [[nodiscard]] const animation::Animator* animator() const noexcept {
        return animator_;
    }

    std::string_view id() const noexcept override {
        return "sibling.animation";
    }
    std::string_view title() const noexcept override { return "Animation"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Number of rows emitted by the most recent `render()`.
    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }

private:
    const animation::Animator* animator_{nullptr};
    std::size_t                lastRows_{0};
};

} // namespace threadmaxx::studio
