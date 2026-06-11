/// @file test_ui_layout_column.cpp
/// @brief Pins `resolveColumn` — same algorithm as resolveRow but on the Y
/// axis. Cross-axis (width) is fixed to the parent's content width.

#include "Check.hpp"

#include <array>

#include "threadmaxx_ui/layout.hpp"

int main() {
    using namespace threadmaxx::ui;

    // 1) fixed + flex + fixed in a 200-tall parent.
    {
        const std::array<Size, 3> sizes = {Size::fixed(40), Size::flex(1.0f), Size::fixed(20)};
        std::array<Rect, 3> out{};
        resolveColumn(Rect{0, 0, 100, 200}, sizes, out);
        CHECK_EQ(out[0], (Rect{0, 0,   100, 40}));
        CHECK_EQ(out[1], (Rect{0, 40,  100, 140}));
        CHECK_EQ(out[2], (Rect{0, 180, 100, 20}));
    }

    // 2) Column with spacing=4 between three 30 px fixed items.
    {
        const std::array<Size, 3> sizes = {Size::fixed(30), Size::fixed(30), Size::fixed(30)};
        std::array<Rect, 3> out{};
        resolveColumn(Rect{0, 0, 100, 200}, sizes, out, Padding{}, 4);
        CHECK_EQ(out[0].y, 0);
        CHECK_EQ(out[0].h, 30);
        CHECK_EQ(out[1].y, 34);
        CHECK_EQ(out[1].h, 30);
        CHECK_EQ(out[2].y, 68);
        CHECK_EQ(out[2].h, 30);
    }

    // 3) Padding pushes the column inward.
    {
        const std::array<Size, 2> sizes = {Size::flex(1.0f), Size::flex(1.0f)};
        std::array<Rect, 2> out{};
        resolveColumn(Rect{0, 0, 200, 100}, sizes, out, Padding::uniform(10));
        CHECK_EQ(out[0].x, 10);
        CHECK_EQ(out[0].y, 10);
        CHECK_EQ(out[0].w, 180);
        CHECK_EQ(out[0].h, 40);   // (100 - 20) / 2 = 40
        CHECK_EQ(out[1].y, 50);
        CHECK_EQ(out[1].h, 40);
    }

    EXIT_WITH_RESULT();
}
