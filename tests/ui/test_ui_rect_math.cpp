/// @file test_ui_rect_math.cpp
/// @brief Pins the Rect intersect / contains / union edge cases. Layout
/// (UI2) leans on these heavily.

#include "Check.hpp"

#include "threadmaxx_ui/types.hpp"
#include "threadmaxx_ui/detail/rect_math.hpp"

int main() {
    using namespace threadmaxx::ui;

    // contains
    CHECK(contains(Rect{0, 0, 10, 10}, 0, 0));
    CHECK(contains(Rect{0, 0, 10, 10}, 9, 9));
    CHECK(!contains(Rect{0, 0, 10, 10}, 10, 0));  // bottom-right exclusive
    CHECK(!contains(Rect{0, 0, 10, 10}, -1, 5));

    // intersect — full overlap
    {
        const Rect r = intersect(Rect{0, 0, 10, 10}, Rect{2, 2, 6, 6});
        CHECK_EQ(r, (Rect{2, 2, 6, 6}));
    }

    // intersect — partial
    {
        const Rect r = intersect(Rect{0, 0, 10, 10}, Rect{5, 5, 10, 10});
        CHECK_EQ(r, (Rect{5, 5, 5, 5}));
    }

    // intersect — disjoint
    {
        const Rect r = intersect(Rect{0, 0, 10, 10}, Rect{20, 20, 5, 5});
        CHECK(isEmpty(r));
    }

    // intersect — touching edges (zero overlap)
    {
        const Rect r = intersect(Rect{0, 0, 10, 10}, Rect{10, 0, 5, 10});
        CHECK(isEmpty(r));
    }

    // unionRect — non-overlapping
    {
        const Rect r = unionRect(Rect{0, 0, 5, 5}, Rect{10, 10, 5, 5});
        CHECK_EQ(r, (Rect{0, 0, 15, 15}));
    }

    // unionRect — empty + non-empty is the non-empty
    {
        const Rect r = unionRect(Rect{0, 0, 0, 0}, Rect{2, 3, 4, 5});
        CHECK_EQ(r, (Rect{2, 3, 4, 5}));
    }

    // unionRect — both empty
    {
        const Rect r = unionRect(Rect{}, Rect{});
        CHECK(isEmpty(r));
    }

    // inset — shrinks symmetrically
    {
        const Rect r = detail::inset(Rect{0, 0, 100, 100}, 10);
        CHECK_EQ(r, (Rect{10, 10, 80, 80}));
    }

    // inset — past-zero margin clamps area to zero
    {
        const Rect r = detail::inset(Rect{0, 0, 10, 10}, 100);
        CHECK(isEmpty(r));
    }

    // translate
    {
        const Rect r = detail::translate(Rect{5, 5, 10, 10}, -3, 4);
        CHECK_EQ(r, (Rect{2, 9, 10, 10}));
    }

    EXIT_WITH_RESULT();
}
