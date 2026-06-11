#pragma once

/// @file detail/clip_stack.hpp
/// @brief Fixed-capacity stack of clip rects. Pushed clips are intersected
/// with the parent — what the backend actually receives in `ClipPush` is
/// always the intersection so widgets at the leaf can use the top rect
/// without re-walking the stack.

#include <array>
#include <cassert>
#include <cstddef>

#include "threadmaxx_ui/config.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui::detail {

class ClipStack {
public:
    void reset() noexcept { depth_ = 0; overflowCount_ = 0; }

    /// Push `r` onto the stack; the effective rect is `r ∩ top` so nested
    /// clips never escape their parent. Returns the effective rect.
    Rect push(Rect r) noexcept {
        const Rect effective = depth_ == 0 ? r : intersect(slots_[depth_ - 1], r);
        if (depth_ < slots_.size()) {
            slots_[depth_++] = effective;
        } else {
            ++overflowCount_;
        }
        return effective;
    }

    /// Pop the top rect. No-op if empty.
    void pop() noexcept {
        if (depth_ > 0) --depth_;
    }

    [[nodiscard]] bool empty() const noexcept { return depth_ == 0; }
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }
    [[nodiscard]] std::uint64_t overflowCount() const noexcept { return overflowCount_; }

    /// Returns the topmost clip rect, or an empty rect if the stack is
    /// empty.
    [[nodiscard]] Rect top() const noexcept {
        return depth_ == 0 ? Rect{} : slots_[depth_ - 1];
    }

private:
    std::array<Rect, kClipStackDepth> slots_{};
    std::size_t depth_ = 0;
    std::uint64_t overflowCount_ = 0;
};

} // namespace threadmaxx::ui::detail
