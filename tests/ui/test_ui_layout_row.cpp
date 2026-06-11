/// @file test_ui_layout_row.cpp
/// @brief Pins `resolveRow` — fixed + flex + fixed mixes, all-fixed, all-flex,
/// rounding leftover absorbed by the last flex slot.

#include "Check.hpp"

#include <array>

#include "threadmaxx_ui/layout.hpp"

int main() {
    using namespace threadmaxx::ui;

    // 1) fixed + flex + fixed in a 400-wide parent, no padding, no spacing.
    //    Expected: 80 / 220 / 100.
    {
        const std::array<Size, 3> sizes = {Size::fixed(80), Size::flex(1.0f), Size::fixed(100)};
        std::array<Rect, 3> out{};
        resolveRow(Rect{0, 0, 400, 50}, sizes, out);
        CHECK_EQ(out[0], (Rect{0,   0, 80,  50}));
        CHECK_EQ(out[1], (Rect{80,  0, 220, 50}));
        CHECK_EQ(out[2], (Rect{300, 0, 100, 50}));
    }

    // 2) Same row but with spacing=10. fixed 80 + spacing 10 + flex + spacing
    //    10 + fixed 100 = 200 fixed -> flex = 200.
    {
        const std::array<Size, 3> sizes = {Size::fixed(80), Size::flex(1.0f), Size::fixed(100)};
        std::array<Rect, 3> out{};
        resolveRow(Rect{0, 0, 400, 50}, sizes, out, Padding{}, 10);
        CHECK_EQ(out[0], (Rect{0,   0, 80,  50}));
        CHECK_EQ(out[1], (Rect{90,  0, 200, 50}));
        CHECK_EQ(out[2], (Rect{300, 0, 100, 50}));
    }

    // 3) Two flex siblings sharing 100 px. Last flex absorbs the rounding
    //    leftover so the row has no gap.
    {
        const std::array<Size, 2> sizes = {Size::flex(1.0f), Size::flex(2.0f)};
        std::array<Rect, 2> out{};
        resolveRow(Rect{0, 0, 100, 20}, sizes, out);
        // 1/3 of 100 = 33; remainder (67) goes to the second.
        CHECK_EQ(out[0].w, 33);
        CHECK_EQ(out[1].w, 67);
        CHECK_EQ(out[0].x + out[0].w, out[1].x);
    }

    // 4) Padding shifts the content rect.
    {
        const std::array<Size, 1> sizes = {Size::flex(1.0f)};
        std::array<Rect, 1> out{};
        resolveRow(Rect{0, 0, 100, 30}, sizes, out, Padding::uniform(5));
        CHECK_EQ(out[0], (Rect{5, 5, 90, 20}));
    }

    // 5) Fixed siblings exceeding the parent → flex collapses to zero.
    {
        const std::array<Size, 3> sizes = {Size::fixed(200), Size::flex(1.0f), Size::fixed(200)};
        std::array<Rect, 3> out{};
        resolveRow(Rect{0, 0, 300, 40}, sizes, out);
        CHECK_EQ(out[0].w, 200);
        CHECK_EQ(out[1].w, 0);
        CHECK_EQ(out[2].w, 200);
    }

    EXIT_WITH_RESULT();
}
