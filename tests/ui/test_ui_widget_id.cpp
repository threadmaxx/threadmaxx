/// @file test_ui_widget_id.cpp
/// @brief Pins the WidgetID hashing contract: stable across frames for the
/// same push path, balanced push/pop, and a known FNV-1a-64 reference value
/// on a fixed input.

#include "Check.hpp"

#include "threadmaxx_ui/detail/id_stack.hpp"
#include "threadmaxx_ui/context.hpp"

int main() {
    using namespace threadmaxx::ui;
    using detail::IdStack;
    using detail::fnv1a64;
    using detail::kFnvOffsetBasis;

    // 1) Known FNV-1a-64 reference: the string "threadmaxx" hashes to a
    //    specific value. Catches accidental algorithm drift.
    constexpr std::string_view kInput = "threadmaxx";
    constexpr std::uint64_t kExpected = fnv1a64(kInput);
    CHECK_EQ(kExpected, 0xfcff01fe7a4c3d57ULL);
    (void)kExpected;

    // 2) IdStack push/pop balances. After equal pushes and pops, the top
    //    returns to the base offset basis.
    IdStack s;
    CHECK_EQ(s.depth(), std::size_t{1});
    CHECK_EQ(s.top(), kFnvOffsetBasis);

    const std::uint64_t a = s.pushString("panel");
    const std::uint64_t b = s.pushString("row");
    CHECK(a != kFnvOffsetBasis);
    CHECK(b != a);
    CHECK_EQ(s.depth(), std::size_t{3});

    s.pop();
    CHECK_EQ(s.top(), a);
    s.pop();
    CHECK_EQ(s.top(), kFnvOffsetBasis);

    // 3) Same push path => same WidgetID. Different path => different.
    UIContext ctx;
    ctx.beginFrame();
    ctx.pushId("panel");
    ctx.pushId("button");
    const WidgetID idA = ctx.currentId();
    ctx.popId();
    ctx.popId();
    ctx.endFrame();

    ctx.beginFrame();
    ctx.pushId("panel");
    ctx.pushId("button");
    const WidgetID idB = ctx.currentId();
    ctx.popId();
    ctx.popId();
    ctx.endFrame();

    CHECK_EQ(idA, idB);

    ctx.beginFrame();
    ctx.pushId("panel");
    ctx.pushId("slider");
    const WidgetID idC = ctx.currentId();
    ctx.popId();
    ctx.popId();
    ctx.endFrame();

    CHECK(idA != idC);

    // 4) Int segments distinguish array rows.
    ctx.beginFrame();
    ctx.pushId("list");
    ctx.pushId(static_cast<std::uint64_t>(0));
    const WidgetID row0 = ctx.currentId();
    ctx.popId();
    ctx.pushId(static_cast<std::uint64_t>(1));
    const WidgetID row1 = ctx.currentId();
    ctx.popId();
    ctx.popId();
    ctx.endFrame();
    CHECK(row0 != row1);

    EXIT_WITH_RESULT();
}
