/// @file test_ui_inspect_enum.cpp

#include "Check.hpp"
#include <array>
#include <utility>
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/inspect.hpp"

enum class Mode { Off, Hold, Loop };

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const Rect row{0, 0, 200, 22};

    const std::array<std::pair<Mode, std::string_view>, 3> options = {
        std::pair<Mode, std::string_view>{Mode::Off,  "Off"},
        std::pair<Mode, std::string_view>{Mode::Hold, "Hold"},
        std::pair<Mode, std::string_view>{Mode::Loop, "Loop"}};

    Mode m = Mode::Off;

    auto cycle = [&]() {
        UIInput press;
        press.mousePos = Vec2i{180, 11};
        press.mouseButtons = MouseButton::Left;
        press.mouseButtonsPressed = MouseButton::Left;
        ctx.setInput(press);
        ctx.beginFrame();
        inspectEnum(ctx, WidgetID{0xE0}, row, "Mode", &m,
                    std::span<const std::pair<Mode, std::string_view>>{options.data(), options.size()});
        ctx.endFrame();
        UIInput rel;
        rel.mousePos = Vec2i{180, 11};
        rel.mouseButtonsReleased = MouseButton::Left;
        ctx.setInput(rel);
        ctx.beginFrame();
        inspectEnum(ctx, WidgetID{0xE0}, row, "Mode", &m,
                    std::span<const std::pair<Mode, std::string_view>>{options.data(), options.size()});
        ctx.endFrame();
    };

    cycle();
    CHECK(m == Mode::Hold);
    cycle();
    CHECK(m == Mode::Loop);
    cycle();  // wraps
    CHECK(m == Mode::Off);

    EXIT_WITH_RESULT();
}
