/// @file test_ui_widget_selectable.cpp

#include "Check.hpp"
#include <array>
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    std::array<WidgetID, 10> ids{};
    for (std::uint64_t i = 0; i < ids.size(); ++i) ids[i] = WidgetID{0xE000 + i};
    std::int32_t selected = -1;

    auto clickRow = [&](int rowIdx) {
        const Rect rRow{0, rowIdx * 22, 200, 20};
        UIInput press;
        press.mousePos = Vec2i{10, rowIdx * 22 + 5};
        press.mouseButtons = MouseButton::Left;
        press.mouseButtonsPressed = MouseButton::Left;
        ctx.setInput(press);
        ctx.beginFrame();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const Rect rowR{0, static_cast<std::int32_t>(i * 22), 200, 20};
            selectable(ctx, ids[i], rowR, "item", selected == static_cast<std::int32_t>(i));
        }
        ctx.endFrame();
        UIInput rel;
        rel.mousePos = Vec2i{10, rowIdx * 22 + 5};
        rel.mouseButtonsReleased = MouseButton::Left;
        ctx.setInput(rel);
        ctx.beginFrame();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const Rect rowR{0, static_cast<std::int32_t>(i * 22), 200, 20};
            if (selectable(ctx, ids[i], rowR, "item",
                           selected == static_cast<std::int32_t>(i))) {
                selected = static_cast<std::int32_t>(i);
            }
        }
        ctx.endFrame();
        (void)rRow;
    };

    clickRow(3);
    CHECK_EQ(selected, 3);
    clickRow(7);
    CHECK_EQ(selected, 7);
    clickRow(0);
    CHECK_EQ(selected, 0);

    // Selection survives a non-click frame.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const Rect rowR{0, static_cast<std::int32_t>(i * 22), 200, 20};
        selectable(ctx, ids[i], rowR, "item",
                   selected == static_cast<std::int32_t>(i));
    }
    ctx.endFrame();
    CHECK_EQ(selected, 0);

    EXIT_WITH_RESULT();
}
