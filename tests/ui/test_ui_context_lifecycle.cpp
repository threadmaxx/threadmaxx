/// @file test_ui_context_lifecycle.cpp
/// @brief Pins the beginFrame/endFrame round trip + frame counter +
/// backend dispatch.

#include "Check.hpp"

#include "threadmaxx_ui/backends/NullBackend.hpp"
#include "threadmaxx_ui/context.hpp"

int main() {
    using namespace threadmaxx::ui;

    NullBackend backend;
    UIContext ctx;
    CHECK(ctx.state() == FrameState::Idle);
    CHECK(!ctx.inFrame());
    CHECK_EQ(ctx.frameCount(), std::uint64_t{0});
    CHECK(ctx.backend() == nullptr);

    ctx.setBackend(&backend);
    CHECK(ctx.backend() == &backend);

    // Three full frames — backend submitCount and frameCount move in step.
    for (int i = 0; i < 3; ++i) {
        ctx.beginFrame();
        CHECK(ctx.inFrame());
        CHECK(ctx.state() == FrameState::Active);
        ctx.drawList().emitRect(Rect{0, 0, 1, 1}, Color{255, 0, 0, 255});
        ctx.endFrame();
        CHECK(!ctx.inFrame());
    }
    CHECK_EQ(ctx.frameCount(), std::uint64_t{3});
    CHECK_EQ(backend.submitCount(), std::uint64_t{3});
    CHECK_EQ(backend.lastCommands(), std::size_t{1});

    // ID stack collapses back to base each frame.
    ctx.beginFrame();
    CHECK_EQ(ctx.idStackDepth(), std::size_t{1});
    ctx.pushId("a");
    ctx.pushId("b");
    CHECK_EQ(ctx.idStackDepth(), std::size_t{3});
    ctx.endFrame();
    ctx.beginFrame();
    CHECK_EQ(ctx.idStackDepth(), std::size_t{1});
    ctx.endFrame();

    // Detaching the backend short-circuits dispatch without losing the
    // frame counter.
    ctx.setBackend(nullptr);
    const std::uint64_t before = backend.submitCount();
    ctx.beginFrame();
    ctx.endFrame();
    CHECK_EQ(backend.submitCount(), before);
    CHECK_EQ(ctx.frameCount(), std::uint64_t{6});

    EXIT_WITH_RESULT();
}
