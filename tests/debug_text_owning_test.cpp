// §3.6.5 batch 15a — Owning-string DebugText.
//
// RenderFrameBuilder ships TWO overloads for debug text:
//   - `addDebugText(const DebugText&)`     — non-owning, producer keeps
//                                            the string_view alive.
//   - `addDebugText(pos, sv, color)`       — owning, copies into a
//                                            per-builder string arena.
//
// The owning overload is the one game code uses when the text comes
// from a std::format temporary — the producer's string is destroyed
// before submitFrame would read it. Test:
//
//   (1) An owning push survives the user's stack frame collapsing.
//   (2) The resulting string_view in `RenderFrame::debugText` is
//       null-terminator-free but contains the right characters.
//   (3) Multiple pushes are independent; the arena correctly tracks
//       offsets and pointer fixup after reallocations.
//   (4) Mixing owning + non-owning pushes preserves ordering.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;

class TextPushSystem : public ISystem {
public:
    const char* name() const noexcept override { return "text-push"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext&) override {}
    void buildRenderFrame(RenderFrameBuilder& b) override {
        // (1) Owning push from a temporary.
        for (int i = 0; i < 20; ++i) {
            std::string s = "label-" + std::to_string(i);
            b.addDebugText(Vec3{static_cast<float>(i), 0.0f, 0.0f},
                           s, 0xFFFFFFFFu);
        }
        // (4) Interleave a non-owning entry.
        static const char* kStatic = "static-text";
        b.addDebugText(DebugText{Vec3{}, kStatic, 0xFFFFFFFFu});
        // More owning entries after the non-owning one.
        for (int i = 20; i < 30; ++i) {
            std::string s = "tail-" + std::to_string(i);
            b.addDebugText(Vec3{}, s);
        }
    }
};

class TextCheckRenderer : public IRenderer {
public:
    void submitFrame(const RenderFrame& f) override {
        captured.clear();
        for (const auto& d : f.debugText) {
            captured.emplace_back(d.text);
        }
        ++submitCount;
    }
    std::vector<std::string> captured;
    int submitCount = 0;
};

} // namespace

int main() {
    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);
    TextCheckRenderer renderer;
    engine.setRenderer(&renderer);

    struct G : IGame {
        void onSetup(Engine& eng, World&, CommandBuffer&) override {
            eng.registerSystem(std::make_unique<TextPushSystem>());
        }
    } g;
    CHECK(engine.initialize(g));
    engine.step();
    CHECK(renderer.submitCount >= 1);
    // 20 owning + 1 non-owning + 10 owning = 31.
    CHECK_EQ(renderer.captured.size(), std::size_t{31});

    // (2) First 20 are "label-0" .. "label-19".
    for (int i = 0; i < 20; ++i) {
        const std::string expected = "label-" + std::to_string(i);
        CHECK_EQ(renderer.captured[static_cast<std::size_t>(i)], expected);
    }
    // (4) Non-owning slot.
    CHECK_EQ(renderer.captured[20], "static-text");
    // Tail.
    for (int i = 20; i < 30; ++i) {
        const std::string expected = "tail-" + std::to_string(i);
        CHECK_EQ(renderer.captured[static_cast<std::size_t>(i + 1)], expected);
    }

    // (3) Run another tick — the arena should have been reset and
    // rebuilt cleanly.
    engine.step();
    CHECK_EQ(renderer.captured.size(), std::size_t{31});
    CHECK_EQ(renderer.captured[0], "label-0");

    engine.shutdown();
    EXIT_WITH_RESULT();
}
