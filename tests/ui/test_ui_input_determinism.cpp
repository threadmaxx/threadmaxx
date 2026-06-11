/// @file test_ui_input_determinism.cpp
/// @brief Pins the determinism contract: two contexts fed the same input
/// stream and the same interact() call order land on byte-identical draw
/// lists AND identical hover / focus / active state after every frame.

#include "Check.hpp"

#include <array>
#include <cstring>

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"

namespace {

bool drawListsEqual(const threadmaxx::ui::DrawList& a,
                    const threadmaxx::ui::DrawList& b) {
    if (a.commands().size() != b.commands().size()) return false;
    if (a.textBytes().size() != b.textBytes().size()) return false;
    if (a.textBytes().size() > 0) {
        if (std::memcmp(a.textBytes().data(), b.textBytes().data(),
                        a.textBytes().size()) != 0) return false;
    }
    for (std::size_t i = 0; i < a.commands().size(); ++i) {
        const auto& ca = a.commands()[i];
        const auto& cb = b.commands()[i];
        if (ca.kind != cb.kind) return false;
        if (std::memcmp(&ca.payload, &cb.payload, sizeof(ca.payload)) != 0) {
            return false;
        }
    }
    return true;
}

constexpr threadmaxx::ui::WidgetID kWidgets[] = {
    threadmaxx::ui::WidgetID{0x11},
    threadmaxx::ui::WidgetID{0x22},
    threadmaxx::ui::WidgetID{0x33},
    threadmaxx::ui::WidgetID{0x44},
};

void runFrame(threadmaxx::ui::UIContext& ctx) {
    using namespace threadmaxx::ui;
    ctx.beginFrame();
    auto& dl = ctx.drawList();
    for (std::size_t i = 0; i < std::size(kWidgets); ++i) {
        const Rect bounds{static_cast<std::int32_t>(i * 50), 0, 40, 40};
        dl.emitRect(bounds, Color{255, 0, 0, 255});
        const auto r = interact(ctx, kWidgets[i], bounds, HitTestFlags::Focusable);
        if (r.hovered) dl.emitRect(bounds, Color{0, 255, 0, 64}, 1);
        if (r.active)  dl.emitRect(bounds, Color{0, 0, 255, 96}, 1);
    }
    ctx.endFrame();
}

} // namespace

int main() {
    using namespace threadmaxx::ui;

    UIContext a;
    UIContext b;

    // Build a synthetic input stream.
    std::array<UIInput, 100> stream{};
    for (std::size_t i = 0; i < stream.size(); ++i) {
        UIInput& in = stream[i];
        in.mousePos = Vec2i{
            static_cast<std::int32_t>(i * 3),
            static_cast<std::int32_t>(i % 40)};
        if (i % 7 == 0) in.mouseButtonsPressed = MouseButton::Left;
        if (i % 7 == 3) in.mouseButtonsReleased = MouseButton::Left;
        if (i % 7 >= 0 && i % 7 < 3) in.mouseButtons = MouseButton::Left;
        if (i % 11 == 0) in.navKeysPressed = NavKey::Tab;
        if (i % 23 == 0) in.navKeysPressed |= NavKey::ShiftTab;
    }

    for (std::size_t f = 0; f < stream.size(); ++f) {
        a.setInput(stream[f]);
        b.setInput(stream[f]);
        runFrame(a);
        runFrame(b);

        // After every frame the two contexts must agree on everything.
        CHECK(drawListsEqual(a.drawList(), b.drawList()));
        CHECK_EQ(hoveredId(a), hoveredId(b));
        CHECK_EQ(activeId(a),  activeId(b));
        CHECK_EQ(focusedId(a), focusedId(b));
    }

    EXIT_WITH_RESULT();
}
