/// @file test_ui_layout_padding_spacing.cpp
/// @brief Pins `applyPadding` + spacing arithmetic edges.

#include "Check.hpp"

#include <array>

#include "threadmaxx_ui/layout.hpp"

int main() {
    using namespace threadmaxx::ui;

    // applyPadding — uniform shrink.
    {
        const Rect r = applyPadding(Rect{0, 0, 100, 100}, Padding::uniform(10));
        CHECK_EQ(r, (Rect{10, 10, 80, 80}));
    }

    // applyPadding — asymmetric.
    {
        const Rect r = applyPadding(Rect{0, 0, 100, 60},
                                    Padding{5, 8, 7, 2});
        CHECK_EQ(r, (Rect{5, 8, 88, 50}));
    }

    // applyPadding — past-zero clamps to zero.
    {
        const Rect r = applyPadding(Rect{0, 0, 10, 10}, Padding::uniform(100));
        CHECK(isEmpty(r));
    }

    // Zero-spacing path is identical to passing no spacing.
    {
        const std::array<Size, 3> sizes = {
            Size::fixed(50), Size::fixed(50), Size::fixed(50)};
        std::array<Rect, 3> a{};
        std::array<Rect, 3> b{};
        resolveRow(Rect{0, 0, 200, 20}, sizes, a, Padding{}, 0);
        resolveRow(Rect{0, 0, 200, 20}, sizes, b);
        CHECK_EQ(a[0], b[0]);
        CHECK_EQ(a[1], b[1]);
        CHECK_EQ(a[2], b[2]);
    }

    // Single-child row — no spacing subtraction.
    {
        const std::array<Size, 1> sizes = {Size::flex(1.0f)};
        std::array<Rect, 1> out{};
        resolveRow(Rect{0, 0, 100, 30}, sizes, out, Padding{}, 50);
        CHECK_EQ(out[0].w, 100);
    }

    EXIT_WITH_RESULT();
}
