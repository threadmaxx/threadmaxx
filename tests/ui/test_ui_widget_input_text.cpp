/// @file test_ui_widget_input_text.cpp

#include "Check.hpp"
#include <cstring>
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID id{0x71};
    const Rect r{0, 0, 200, 20};
    char buf[16] = {0};

    // Frame 1: Tab to focus the input.
    UIInput tab;
    tab.navKeysPressed = NavKey::Tab;
    ctx.setInput(tab);
    ctx.beginFrame();
    inputText(ctx, id, r, buf, sizeof(buf));
    ctx.endFrame();
    CHECK_EQ(focusedId(ctx), id);

    // Frame 2: type "hi".
    UIInput type;
    type.chars[0] = 'h';
    type.chars[1] = 'i';
    type.charsCount = 2;
    ctx.setInput(type);
    ctx.beginFrame();
    CHECK(!inputText(ctx, id, r, buf, sizeof(buf)));
    ctx.endFrame();
    CHECK_EQ(std::strcmp(buf, "hi"), 0);

    // Frame 3: type ' ' + "world".
    UIInput more;
    more.chars[0] = ' ';
    more.chars[1] = 'w';
    more.chars[2] = 'o';
    more.chars[3] = 'r';
    more.chars[4] = 'l';
    more.chars[5] = 'd';
    more.charsCount = 6;
    ctx.setInput(more);
    ctx.beginFrame();
    inputText(ctx, id, r, buf, sizeof(buf));
    ctx.endFrame();
    CHECK_EQ(std::strcmp(buf, "hi world"), 0);

    // Frame 4: backspace trims the last char.
    UIInput bs;
    bs.navKeysPressed = NavKey::Backspace;
    ctx.setInput(bs);
    ctx.beginFrame();
    inputText(ctx, id, r, buf, sizeof(buf));
    ctx.endFrame();
    CHECK_EQ(std::strcmp(buf, "hi worl"), 0);

    // Frame 5: Enter commits.
    UIInput enter;
    enter.navKeysPressed = NavKey::Enter;
    ctx.setInput(enter);
    ctx.beginFrame();
    CHECK(inputText(ctx, id, r, buf, sizeof(buf)));
    ctx.endFrame();

    // Capacity guard — flood with chars, buffer never overflows.
    UIInput flood;
    for (std::size_t i = 0; i < kMaxFrameChars; ++i) flood.chars[i] = 'A';
    flood.charsCount = kMaxFrameChars;
    ctx.setInput(flood);
    for (int i = 0; i < 10; ++i) {
        ctx.beginFrame();
        inputText(ctx, id, r, buf, sizeof(buf));
        ctx.endFrame();
    }
    CHECK(std::strlen(buf) <= sizeof(buf) - 1);

    EXIT_WITH_RESULT();
}
