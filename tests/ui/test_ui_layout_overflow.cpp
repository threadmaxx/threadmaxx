/// @file test_ui_layout_overflow.cpp
/// @brief Pins the silent-truncate-on-overflow behavior of the layout stack
/// and the clip stack. Both increment an overflow counter rather than
/// crashing — the host can choose to assert on the counter at end of frame.

#include "Check.hpp"

#include "threadmaxx_ui/config.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/layout.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    ctx.beginFrame();

    // Push the layout stack right up to its cap — every push succeeds.
    for (std::size_t i = 0; i < kLayoutStackDepth; ++i) {
        const std::int32_t idx = pushLayout(ctx, Rect{0, 0, 100, 100},
                                            Orientation::Column);
        CHECK(idx != kLayoutOverflowed);
    }
    CHECK_EQ(ctx.layoutDepth(), kLayoutStackDepth);
    CHECK_EQ(ctx.layoutOverflowCount(), std::uint64_t{0});

    // One more push — must report overflow and not crash.
    const std::int32_t over1 = pushLayout(ctx, Rect{0, 0, 100, 100},
                                          Orientation::Column);
    CHECK_EQ(over1, kLayoutOverflowed);
    CHECK_EQ(ctx.layoutDepth(), kLayoutStackDepth);
    CHECK_EQ(ctx.layoutOverflowCount(), std::uint64_t{1});

    const std::int32_t over2 = pushLayout(ctx, Rect{0, 0, 100, 100},
                                          Orientation::Column);
    CHECK_EQ(over2, kLayoutOverflowed);
    CHECK_EQ(ctx.layoutOverflowCount(), std::uint64_t{2});

    // Popping the cap-deep stack back to empty leaves no residue.
    for (std::size_t i = 0; i < kLayoutStackDepth; ++i) {
        popLayout(ctx);
    }
    CHECK_EQ(ctx.layoutDepth(), std::size_t{0});

    // Extra pops on an empty stack are silent no-ops.
    popLayout(ctx);
    popLayout(ctx);
    CHECK_EQ(ctx.layoutDepth(), std::size_t{0});

    // Clip stack — same shape.
    for (std::size_t i = 0; i < kClipStackDepth; ++i) {
        pushClip(ctx, Rect{0, 0, 100, 100});
    }
    CHECK_EQ(ctx.clipStack().depth(), kClipStackDepth);
    CHECK_EQ(ctx.clipStack().overflowCount(), std::uint64_t{0});

    pushClip(ctx, Rect{0, 0, 100, 100});
    CHECK_EQ(ctx.clipStack().depth(), kClipStackDepth);
    CHECK_EQ(ctx.clipStack().overflowCount(), std::uint64_t{1});

    for (std::size_t i = 0; i < kClipStackDepth; ++i) {
        popClip(ctx);
    }
    CHECK_EQ(ctx.clipStack().depth(), std::size_t{0});

    ctx.endFrame();

    EXIT_WITH_RESULT();
}
