/// @file test_ui_layout_nested.cpp
/// @brief Pins nested resolution: a column whose middle child is itself a
/// row. Verifies the leaf rects land at the expected absolute pixel coords.

#include "Check.hpp"

#include <array>

#include "threadmaxx_ui/layout.hpp"

int main() {
    using namespace threadmaxx::ui;

    // Outer panel: 400x300 at origin. Three columns: header (30 px), body
    // (flex), footer (20 px), no spacing.
    const std::array<Size, 3> outerSizes = {
        Size::fixed(30), Size::flex(1.0f), Size::fixed(20)};
    std::array<Rect, 3> outer{};
    resolveColumn(Rect{0, 0, 400, 300}, outerSizes, outer);
    CHECK_EQ(outer[1], (Rect{0, 30, 400, 250}));

    // Body is split into a left sidebar (100 px) and a content pane (flex).
    const std::array<Size, 2> bodySizes = {Size::fixed(100), Size::flex(1.0f)};
    std::array<Rect, 2> body{};
    resolveRow(outer[1], bodySizes, body);
    CHECK_EQ(body[0], (Rect{0,   30, 100, 250}));
    CHECK_EQ(body[1], (Rect{100, 30, 300, 250}));

    // Sidebar split into three equal rows. Each row should be 250/3 = 83.
    // Last row absorbs the rounding leftover (= 84 since 83+83+84=250).
    const std::array<Size, 3> sidebar = {
        Size::flex(1.0f), Size::flex(1.0f), Size::flex(1.0f)};
    std::array<Rect, 3> rows{};
    resolveColumn(body[0], sidebar, rows);
    CHECK_EQ(rows[0].h, 83);
    CHECK_EQ(rows[1].h, 83);
    CHECK_EQ(rows[2].h, 84);
    // Rows are contiguous and inside the sidebar.
    CHECK_EQ(rows[0].y, 30);
    CHECK_EQ(rows[1].y, 113);
    CHECK_EQ(rows[2].y, 196);
    CHECK_EQ(rows[2].y + rows[2].h, 280);

    EXIT_WITH_RESULT();
}
