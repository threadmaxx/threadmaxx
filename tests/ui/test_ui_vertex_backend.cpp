/// @file test_ui_vertex_backend.cpp
/// @brief Pins the VertexBackend tessellation shape.

#include "Check.hpp"
#include "threadmaxx_ui/backends/VertexBackend.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/input.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    VertexBackend backend;
    ctx.setBackend(&backend);

    // Frame 1: one solid rect -> 4 vertices, 6 indices, 1 draw.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    ctx.drawList().emitRect(Rect{0, 0, 100, 100}, Color{255, 0, 0, 255});
    ctx.endFrame();
    CHECK_EQ(backend.vertices().size(), std::size_t{4});
    CHECK_EQ(backend.indices().size(), std::size_t{6});
    CHECK_EQ(backend.draws().size(), std::size_t{1});

    // Frame 2: three rects + one outline -> one merged solid draw.
    ctx.beginFrame();
    ctx.drawList().emitRect(Rect{0, 0, 10, 10}, Color{255, 0, 0, 255});
    ctx.drawList().emitRect(Rect{10, 0, 10, 10}, Color{0, 255, 0, 255});
    ctx.drawList().emitRect(Rect{20, 0, 10, 10}, Color{0, 0, 255, 255});
    ctx.drawList().emitRect(Rect{30, 0, 10, 10}, Color{255, 255, 0, 255}, 2);
    ctx.endFrame();
    CHECK_EQ(backend.draws().size(), std::size_t{1});
    // 3 filled rects (12 verts, 18 indices) + 1 outline (4 sides × 4 verts =
    // 16 verts, 4 × 6 indices = 24).
    CHECK_EQ(backend.vertices().size(), std::size_t{12 + 16});
    CHECK_EQ(backend.indices().size(), std::size_t{18 + 24});

    // Frame 3: clip push/pop forces new draw calls.
    ctx.beginFrame();
    ctx.drawList().emitRect(Rect{0, 0, 100, 100}, Color{255, 0, 0, 255});
    ctx.drawList().emitClipPush(Rect{10, 10, 50, 50});
    ctx.drawList().emitRect(Rect{20, 20, 30, 30}, Color{0, 255, 0, 255});
    ctx.drawList().emitClipPop();
    ctx.drawList().emitRect(Rect{60, 60, 30, 30}, Color{0, 0, 255, 255});
    ctx.endFrame();
    CHECK_EQ(backend.draws().size(), std::size_t{3});  // distinct scissors

    // Frame 4: text -> separate FontAtlas draw.
    ctx.beginFrame();
    ctx.drawList().emitRect(Rect{0, 0, 10, 10}, Color{255, 0, 0, 255});
    ctx.drawList().emitText(Vec2i{0, 0}, Color{255, 255, 255, 255}, "ABC");
    ctx.endFrame();
    CHECK_EQ(backend.draws().size(), std::size_t{2});  // solid + font

    // Frame 5: image -> separate Image draw with imageHandle propagated.
    ctx.beginFrame();
    ctx.drawList().emitImage(Rect{0, 0, 16, 16}, 0x1234);
    ctx.endFrame();
    CHECK_EQ(backend.draws().size(), std::size_t{1});
    CHECK(backend.draws()[0].kind == VertexDrawKind::Image);
    CHECK_EQ(backend.draws()[0].imageHandle, std::uint32_t{0x1234});

    EXIT_WITH_RESULT();
}
