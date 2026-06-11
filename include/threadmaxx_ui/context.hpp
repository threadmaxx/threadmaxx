#pragma once

/// @file context.hpp
/// @brief `UIContext` — per-frame state owner. Holds the ID stack, the draw
/// list, the backend pointer, and (in later batches) the layout / input /
/// widget-state tables. Construct one per editor pane / overlay; the
/// library never reaches for a global.
///
/// Frame lifecycle:
///
///   ctx.beginFrame();
///   // ... widget calls ...
///   ctx.endFrame();   // hands the draw list to the backend
///
/// Calling `beginFrame()` twice in a row asserts; widgets emitted outside a
/// frame are also flagged.

#include <cassert>
#include <cstdint>
#include <string_view>

#include "threadmaxx_ui/backend.hpp"
#include "threadmaxx_ui/detail/id_stack.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

/// State of the per-frame context. Tests pin transitions through this.
enum class FrameState : std::uint8_t {
    Idle    = 0,
    Active  = 1,
};

class UIContext {
public:
    UIContext() = default;

    /// Set or replace the backend. `nullptr` means "drop the draw list on
    /// `endFrame()`". The context does not take ownership.
    void setBackend(IUIBackend* backend) noexcept { backend_ = backend; }
    [[nodiscard]] IUIBackend* backend() const noexcept { return backend_; }

    /// Open a new frame. Resets the ID stack to base and clears the draw
    /// list (capacity preserved). Asserts if already inside a frame.
    void beginFrame() noexcept {
        assert(state_ == FrameState::Idle && "beginFrame called while already inside a frame");
        ids_.reset();
        drawList_.clear();
        state_ = FrameState::Active;
        ++frameCount_;
    }

    /// Close the current frame and hand the draw list to the backend.
    /// Asserts if no frame is open.
    void endFrame() noexcept {
        assert(state_ == FrameState::Active && "endFrame called without a matching beginFrame");
        if (backend_) {
            backend_->submit(drawList_);
        }
        state_ = FrameState::Idle;
    }

    [[nodiscard]] bool inFrame() const noexcept { return state_ == FrameState::Active; }
    [[nodiscard]] FrameState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t frameCount() const noexcept { return frameCount_; }

    // -- ID stack -----------------------------------------------------------

    /// Push a string segment onto the ID stack. Returns the new top ID.
    WidgetID pushId(std::string_view label) noexcept {
        return WidgetID{ids_.pushString(label)};
    }

    /// Push an integer segment (eg. per-row index in a list).
    WidgetID pushId(std::uint64_t v) noexcept {
        return WidgetID{ids_.pushInt(v)};
    }

    /// Push an already-computed `WidgetID` (lets the caller compose IDs
    /// without touching the underlying hash).
    WidgetID pushId(WidgetID id) noexcept {
        return WidgetID{ids_.pushHash(id.value)};
    }

    /// Pop the top of the ID stack.
    void popId() noexcept { ids_.pop(); }

    /// Current top of the ID stack — useful as the cache key for widget
    /// state (focus, hover, drag).
    [[nodiscard]] WidgetID currentId() const noexcept { return ids_.currentId(); }
    [[nodiscard]] std::size_t idStackDepth() const noexcept { return ids_.depth(); }

    // -- Draw list ----------------------------------------------------------

    /// Mutable draw list, intended for widget implementations. External code
    /// should not normally touch this directly.
    [[nodiscard]] DrawList& drawList() noexcept { return drawList_; }
    [[nodiscard]] const DrawList& drawList() const noexcept { return drawList_; }

private:
    detail::IdStack ids_{};
    DrawList drawList_{};
    IUIBackend* backend_ = nullptr;
    FrameState state_ = FrameState::Idle;
    std::uint64_t frameCount_ = 0;
};

} // namespace threadmaxx::ui
