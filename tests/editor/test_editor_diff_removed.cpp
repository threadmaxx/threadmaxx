/// @file test_editor_diff_removed.cpp
/// @brief E9 — diff against a snapshot missing N entities returns N
/// Removed entries.

#include "Check.hpp"

#include <threadmaxx_editor/diff.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/System.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    std::vector<threadmaxx::EntityHandle> handles;
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        for (int i = 0; i < 5; ++i) {
            auto h = engine.reserveEntityHandle();
            handles.push_back(h);
            seed.spawn(h, threadmaxx::Transform{});
        }
    }
};

struct OneShotDestroyer final : threadmaxx::ISystem {
    std::vector<threadmaxx::EntityHandle> kills;
    bool done{false};
    explicit OneShotDestroyer(std::vector<threadmaxx::EntityHandle> k)
        : kills(std::move(k)) {}
    const char* name() const noexcept override { return "OneShotDestroyer"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        auto local = kills;
        ctx.single([local](threadmaxx::Range,
                           threadmaxx::CommandBuffer& cb) {
            for (auto h : local) cb.destroy(h);
        });
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();

    auto before = engine.world().snapshot();
    CHECK_EQ(before.size(), 5u);

    // Destroy the first 2 entities.
    std::vector<threadmaxx::EntityHandle> kills{game.handles[0],
                                                game.handles[1]};
    engine.registerSystem(std::make_unique<OneShotDestroyer>(kills));
    engine.step();

    auto after = engine.world().snapshot();
    CHECK_EQ(after.size(), 3u);

    const auto r = threadmaxx::editor::WorldDiff::compute(before, after);
    int removed = 0;
    for (const auto& e : r.entries) {
        if (e.kind == threadmaxx::editor::DiffKind::Removed) ++removed;
    }
    CHECK_EQ(removed, 2);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
