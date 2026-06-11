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

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "threadmaxx_ui/backend.hpp"
#include "threadmaxx_ui/config.hpp"
#include "threadmaxx_ui/detail/clip_stack.hpp"
#include "threadmaxx_ui/detail/id_stack.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

struct UIInput;

/// Padding around a layout frame's content rect (per-edge).
struct Padding {
    std::int32_t left   = 0;
    std::int32_t top    = 0;
    std::int32_t right  = 0;
    std::int32_t bottom = 0;

    [[nodiscard]] static constexpr Padding uniform(std::int32_t v) noexcept {
        return Padding{v, v, v, v};
    }
};

/// Layout orientation — primary axis the row/column resolves along.
enum class Orientation : std::uint8_t {
    Row    = 0,
    Column = 1,
};

/// State of one stack frame in the layout stack.
struct LayoutFrame {
    /// Rect inside the padding — the area available to children.
    Rect content{};
    Padding padding{};
    std::int32_t spacing = 0;
    Orientation orient = Orientation::Column;
};

/// State of the per-frame context. Tests pin transitions through this.
enum class FrameState : std::uint8_t {
    Idle    = 0,
    Active  = 1,
};

/// One registered hit-test region. UI3 populates these inside `interact()`.
struct HitTestRecord {
    WidgetID id{};
    Rect bounds{};
    std::uint32_t flags = 0;
};

/// Per-widget retained state. Lives across frames; keyed by `WidgetID`.
/// Widget impls use the fields as needed — checkbox/slider don't need
/// retained state (value is bound to the caller's variable), but
/// drag-scalar, input-text, tree node, tooltip all do.
struct WidgetState {
    /// Generic integer slot. Used by:
    ///   input text: cursor position in the buffer.
    ///   tree node : 0=closed, 1=open.
    ///   selectable: 1 once "press inside" has fired (sticky activation).
    std::int64_t iv = 0;
    /// Generic float slot. Used by:
    ///   drag scalar: accumulated drag delta.
    ///   tooltip    : seconds the widget has been hovered.
    double dv = 0.0;
    /// Last frame this widget reported in. Hosts can GC stale entries by
    /// pruning anything with `lastSeenFrame < currentFrame - threshold`.
    std::uint64_t lastSeenFrame = 0;
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
        layoutDepth_ = 0;
        clipStack_.reset();
        hitTests_.clear();
        hoveredId_ = WidgetID{};
        focusKeyboardCapture_ = false;
        focusableCount_ = 0;
        state_ = FrameState::Active;
        ++frameCount_;
    }

    /// Close the current frame and hand the draw list to the backend.
    /// Asserts if no frame is open. Also finalizes input state for the
    /// frame (advances Tab focus, drops a stale `activeId_` if its widget
    /// was not re-registered this frame).
    void endFrame() noexcept {
        assert(state_ == FrameState::Active && "endFrame called without a matching beginFrame");
        finalizeInputState();
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

    // -- Layout / clip stacks (touched by layout.hpp APIs) ------------------

    /// Internal accessor — exposed for `pushLayout` / `popLayout` /
    /// `currentLayout` in `layout.hpp`. Game code uses the wrappers.
    void layoutPushRaw(const LayoutFrame& frame) noexcept;
    void layoutPopRaw() noexcept;
    [[nodiscard]] std::size_t layoutDepth() const noexcept { return layoutDepth_; }
    [[nodiscard]] std::uint64_t layoutOverflowCount() const noexcept { return layoutOverflows_; }
    [[nodiscard]] const LayoutFrame& layoutTop() const noexcept;

    [[nodiscard]] detail::ClipStack& clipStack() noexcept { return clipStack_; }
    [[nodiscard]] const detail::ClipStack& clipStack() const noexcept { return clipStack_; }

    // -- Input / interaction state (touched by input.hpp APIs) --------------

    /// Set the input snapshot for the upcoming frame. Call BEFORE
    /// `beginFrame()`; the snapshot is borrowed by reference during the
    /// frame.
    void setInput(const UIInput& in) noexcept { input_ = &in; }
    [[nodiscard]] const UIInput* input() const noexcept { return input_; }

    [[nodiscard]] std::vector<HitTestRecord>& hitTests() noexcept { return hitTests_; }
    [[nodiscard]] const std::vector<HitTestRecord>& hitTests() const noexcept { return hitTests_; }

    [[nodiscard]] WidgetID hoveredId() const noexcept { return hoveredId_; }
    void setHoveredId(WidgetID id) noexcept { hoveredId_ = id; }

    [[nodiscard]] WidgetID activeId() const noexcept { return activeId_; }
    void setActiveId(WidgetID id) noexcept { activeId_ = id; }

    [[nodiscard]] WidgetID focusedId() const noexcept { return focusedId_; }
    void setFocusedId(WidgetID id) noexcept { focusedId_ = id; }

    [[nodiscard]] bool focusKeyboardCapture() const noexcept { return focusKeyboardCapture_; }
    void setFocusKeyboardCapture(bool v) noexcept { focusKeyboardCapture_ = v; }

    [[nodiscard]] std::uint32_t focusableCount() const noexcept { return focusableCount_; }
    void bumpFocusableCount() noexcept { ++focusableCount_; }

    // -- Popup / menu-bar state (touched by menu.hpp) -----------------------

    [[nodiscard]] WidgetID popupOpenId() const noexcept { return popupOpenId_; }
    void setPopupOpenId(WidgetID id) noexcept { popupOpenId_ = id; }
    [[nodiscard]] std::int32_t popupBodyDepth() const noexcept { return popupBodyDepth_; }
    void pushPopupBody() noexcept { ++popupBodyDepth_; }
    void popPopupBody() noexcept { if (popupBodyDepth_ > 0) --popupBodyDepth_; }

    [[nodiscard]] WidgetID menuBarOpenId() const noexcept { return menuBarOpenId_; }
    void setMenuBarOpenId(WidgetID id) noexcept { menuBarOpenId_ = id; }
    [[nodiscard]] bool menuBarActive() const noexcept { return menuBarActive_; }
    void setMenuBarActive(bool v) noexcept { menuBarActive_ = v; }

    /// Reserve `n` slots in the hit-test vector — tests use it to avoid
    /// per-frame growth in the no-alloc gate.
    void reserveHitTests(std::size_t n) { hitTests_.reserve(n); }

    /// Look up a widget's retained state. Creates the entry on first
    /// touch (this allocates if the map needs to grow — host should call
    /// `reserveWidgetStates(n)` before any no-alloc-tested frames).
    /// Updates `lastSeenFrame` so a GC pass can drop dead entries.
    [[nodiscard]] WidgetState& widgetState(WidgetID id) {
        auto& slot = widgetStates_[id.value];
        slot.lastSeenFrame = frameCount_;
        return slot;
    }

    /// Probe the state without creating an entry. Returns nullptr when the
    /// widget has never been seen.
    [[nodiscard]] const WidgetState* tryGetWidgetState(WidgetID id) const noexcept {
        const auto it = widgetStates_.find(id.value);
        return (it == widgetStates_.end()) ? nullptr : &it->second;
    }

    /// Pre-size the widget-state map so subsequent `widgetState(id)` calls
    /// don't trigger rehashes. Pair with the no-alloc warmup in tests.
    void reserveWidgetStates(std::size_t n) { widgetStates_.reserve(n); }

private:
    void finalizeInputState() noexcept;

    detail::IdStack ids_{};
    DrawList drawList_{};
    IUIBackend* backend_ = nullptr;
    FrameState state_ = FrameState::Idle;
    std::uint64_t frameCount_ = 0;

    // Layout stack — fixed capacity from config; standard-layout POD slots.
    std::array<LayoutFrame, kLayoutStackDepth> layoutStack_{};
    std::size_t layoutDepth_ = 0;
    std::uint64_t layoutOverflows_ = 0;

    detail::ClipStack clipStack_{};

    // Input state.
    const UIInput* input_ = nullptr;
    std::vector<HitTestRecord> hitTests_{};
    WidgetID hoveredId_{};
    WidgetID activeId_{};
    WidgetID focusedId_{};
    bool focusKeyboardCapture_ = false;
    std::uint32_t focusableCount_ = 0;

    // Popup / menu-bar state.
    WidgetID popupOpenId_{};
    std::int32_t popupBodyDepth_ = 0;
    WidgetID menuBarOpenId_{};
    bool menuBarActive_ = false;

    // Retained widget state, keyed by WidgetID::value.
    std::unordered_map<std::uint64_t, WidgetState> widgetStates_{};
};

} // namespace threadmaxx::ui
