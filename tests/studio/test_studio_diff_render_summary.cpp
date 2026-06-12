/// @file test_studio_diff_render_summary.cpp
/// @brief ST9 — WorldDiffPanel takes two named snapshot slots,
/// computes the diff between them via editor::WorldDiff, and renders
/// one row per WorldDiffEntry. Pins the picker → compute → render
/// chain.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/diff.hpp>
#include <threadmaxx_studio/panels/world_diff.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/World.hpp>

#include <memory>

namespace {

struct SeedGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        for (int i = 0; i < 3; ++i) {
            seed.spawn(threadmaxx::Transform{});
        }
    }
};

class Adder final : public threadmaxx::ISystem {
public:
    bool done = false;
    const char* name() const noexcept override { return "Adder"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        ctx.single([](threadmaxx::Range,
                      threadmaxx::CommandBuffer& cb) {
            for (int i = 0; i < 5; ++i) {
                cb.spawn(threadmaxx::Transform{});
            }
        });
    }
};

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    engine.registerSystem(std::make_unique<Adder>());
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::studio::WorldDiffPanel panel;

    // Initial render before any compute: shows the placeholder.
    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    NullSource source;
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    bool foundPlaceholder = false;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText &&
            op.text.find("(no diff)") != std::string::npos) {
            foundPlaceholder = true;
        }
    }
    CHECK(foundPlaceholder);

    // Slot A: post-seed state (8 entities — 3 from seed + 5 from Adder).
    panel.setSnapshot("after", engine.world().snapshot());

    // Slot B: a baseline from a fresh engine with just the seed.
    threadmaxx::Engine engine2{threadmaxx::Config{}};
    SeedGame seed2;
    CHECK(engine2.initialize(seed2));
    engine2.step();
    panel.setSnapshot("before", engine2.world().snapshot());

    CHECK_EQ(panel.slotCount(), 2u);

    // Compute against an unknown slot fails.
    CHECK(!panel.computeDiff("before", "unknown"));
    CHECK(!panel.computeDiff("unknown", "after"));

    // Real diff.
    CHECK(panel.computeDiff("before", "after"));
    CHECK(panel.lastDiff().size() >= 5u);

    // Render emits one drawText per diff entry.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    std::size_t textOps = 0;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) {
            ++textOps;
        }
    }
    CHECK_EQ(textOps, panel.lastDiff().size());

    backend.shutdown();
    engine.shutdown();
    engine2.shutdown();
    EXIT_WITH_RESULT();
}
