/// @file test_ui_input_focus.cpp
/// @brief Pins Tab / Shift-Tab focus cycling through focusable widgets in
/// registration order. Also pins focus survival across re-orderings: a
/// widget's focus persists as long as its WidgetID still appears somewhere.

#include "Check.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"

namespace {

constexpr threadmaxx::ui::WidgetID kA{0xA1};
constexpr threadmaxx::ui::WidgetID kB{0xB2};
constexpr threadmaxx::ui::WidgetID kC{0xC3};

void runFrameInOrder(threadmaxx::ui::UIContext& ctx,
                     std::initializer_list<threadmaxx::ui::WidgetID> ids) {
    ctx.beginFrame();
    int i = 0;
    for (auto id : ids) {
        ctx.pushId(static_cast<std::uint64_t>(i++));
        interact(ctx, id, threadmaxx::ui::Rect{0, 0, 10, 10},
                 threadmaxx::ui::HitTestFlags::Focusable);
        ctx.popId();
    }
    ctx.endFrame();
}

} // namespace

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;

    // Frame 1: register A, B, C focusable; press Tab. At endFrame focus
    // advances from "none" to the first focusable = A.
    UIInput tab;
    tab.navKeysPressed = NavKey::Tab;
    ctx.setInput(tab);
    runFrameInOrder(ctx, {kA, kB, kC});
    CHECK_EQ(focusedId(ctx), kA);

    // Frame 2: Tab again -> B.
    ctx.setInput(tab);
    runFrameInOrder(ctx, {kA, kB, kC});
    CHECK_EQ(focusedId(ctx), kB);

    // Frame 3: Tab again -> C.
    ctx.setInput(tab);
    runFrameInOrder(ctx, {kA, kB, kC});
    CHECK_EQ(focusedId(ctx), kC);

    // Frame 4: Tab again wraps to A.
    ctx.setInput(tab);
    runFrameInOrder(ctx, {kA, kB, kC});
    CHECK_EQ(focusedId(ctx), kA);

    // Frame 5: Shift-Tab walks backwards -> C (wraps).
    UIInput shiftTab;
    shiftTab.navKeysPressed = NavKey::ShiftTab;
    ctx.setInput(shiftTab);
    runFrameInOrder(ctx, {kA, kB, kC});
    CHECK_EQ(focusedId(ctx), kC);

    // Frame 6: Re-order to {C, A, B}. No Tab pressed -> focus persists on C.
    UIInput none;
    ctx.setInput(none);
    runFrameInOrder(ctx, {kC, kA, kB});
    CHECK_EQ(focusedId(ctx), kC);

    // Frame 7: Tab in the new ordering. C is at index 0, next is A.
    ctx.setInput(tab);
    runFrameInOrder(ctx, {kC, kA, kB});
    CHECK_EQ(focusedId(ctx), kA);

    // Frame 8: Tab when no focusables are registered -> no change.
    ctx.setInput(tab);
    ctx.beginFrame();
    ctx.endFrame();
    CHECK_EQ(focusedId(ctx), kA);  // unchanged

    // setFocus / clearFocus.
    setFocus(ctx, kB);
    CHECK_EQ(focusedId(ctx), kB);
    clearFocus(ctx);
    CHECK_EQ(focusedId(ctx), (WidgetID{}));

    EXIT_WITH_RESULT();
}
