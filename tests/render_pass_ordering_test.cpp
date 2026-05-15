// §3.6.5 batch 15b — Per-pass draw-item ordering contract.
//
// Asserts the rules batch 9 (Vulkan reference renderer) will rely on:
//
//   (1) Within a system, draw items appear in `RenderFrame::drawItems[pass]`
//       in the same order they were pushed.
//   (2) Across systems, the registration order is preserved: every
//       draw item pushed by an earlier system precedes every draw
//       item pushed by a later system, within each pass.
//   (3) Each pass index in `RenderFrame::drawItems[]` is independent —
//       pushing into Opaque doesn't affect Transparent ordering.
//   (4) `passIndex(p)` round-trips with the underlying enum so a
//       generated table indexed by pass values can be safely cross-
//       referenced.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>

namespace {

using namespace threadmaxx;

class TagPusher : public ISystem {
public:
    TagPusher(const char* n, std::uint32_t tag) : name_(n), tag_(tag) {}
    const char* name() const noexcept override { return name_; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext&) override {}
    void buildRenderFrame(RenderFrameBuilder& b) override {
        // Push 3 items per pass with a system-distinct tag in
        // `userData` so we can identify each item's origin.
        for (std::uint32_t i = 0; i < 3; ++i) {
            DrawItem item{};
            item.sortKey = (static_cast<std::uint64_t>(tag_) << 32) | i;
            b.addDrawItem(RenderPass::Opaque, item);
        }
        for (std::uint32_t i = 0; i < 2; ++i) {
            DrawItem item{};
            item.sortKey = (static_cast<std::uint64_t>(tag_) << 32) | (10u + i);
            b.addDrawItem(RenderPass::Transparent, item);
        }
        // Only one of the two systems pushes ShadowCasters.
        if (tag_ == 1) {
            DrawItem item{};
            item.sortKey = (static_cast<std::uint64_t>(tag_) << 32) | 100u;
            b.addDrawItem(RenderPass::ShadowCasters, item);
        }
    }
private:
    const char*   name_;
    std::uint32_t tag_;
};

class CaptureRenderer : public IRenderer {
public:
    bool initialize() override { return true; }
    void submitFrame(const RenderFrame& f) override {
        for (std::size_t p = 0; p < kRenderPassCount; ++p) {
            perPass[p].clear();
            for (const auto& d : f.drawItems[p]) {
                perPass[p].push_back(d.sortKey);
            }
        }
        ++submitCount;
    }
    std::array<std::vector<std::uint64_t>, kRenderPassCount> perPass;
    int submitCount = 0;
};

} // namespace

int main() {
    // ---- (4) passIndex round-trip ------------------------------------
    {
        CHECK_EQ(passIndex(RenderPass::Opaque),         std::size_t{0});
        CHECK_EQ(passIndex(RenderPass::Transparent),    std::size_t{1});
        CHECK_EQ(passIndex(RenderPass::ShadowCasters),  std::size_t{2});
        CHECK_EQ(passIndex(RenderPass::Overlay),        std::size_t{3});
        CHECK_EQ(kRenderPassCount,                      std::size_t{4});
    }

    // ---- (1)-(3) per-system + cross-system ordering ------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        CaptureRenderer renderer;
        engine.setRenderer(&renderer);

        struct G : IGame {
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                eng.registerSystem(std::make_unique<TagPusher>("A", 1));
                eng.registerSystem(std::make_unique<TagPusher>("B", 2));
            }
        } g;
        CHECK(engine.initialize(g));
        engine.step();
        // initialize publishes a frame, step publishes another.
        CHECK(renderer.submitCount >= 2);

        const auto& opaque = renderer.perPass[passIndex(RenderPass::Opaque)];
        const auto& transparent = renderer.perPass[passIndex(RenderPass::Transparent)];
        const auto& shadows = renderer.perPass[passIndex(RenderPass::ShadowCasters)];
        const auto& overlay = renderer.perPass[passIndex(RenderPass::Overlay)];

        // Two systems × 3 opaque items each = 6.
        CHECK_EQ(opaque.size(), std::size_t{6});
        // Two systems × 2 transparent items each = 4.
        CHECK_EQ(transparent.size(), std::size_t{4});
        // Only system A pushes a shadow.
        CHECK_EQ(shadows.size(), std::size_t{1});
        CHECK_EQ(overlay.size(), std::size_t{0});

        // Within each system, intra-system order preserved.
        // System A's opaque items appear first (registration order):
        for (std::uint32_t i = 0; i < 3; ++i) {
            const std::uint64_t expected = (1ull << 32) | i;
            CHECK_EQ(opaque[i], expected);
        }
        // Then system B's opaque items.
        for (std::uint32_t i = 0; i < 3; ++i) {
            const std::uint64_t expected = (2ull << 32) | i;
            CHECK_EQ(opaque[3 + i], expected);
        }
        // Same for transparent.
        for (std::uint32_t i = 0; i < 2; ++i) {
            const std::uint64_t expected = (1ull << 32) | (10u + i);
            CHECK_EQ(transparent[i], expected);
        }
        for (std::uint32_t i = 0; i < 2; ++i) {
            const std::uint64_t expected = (2ull << 32) | (10u + i);
            CHECK_EQ(transparent[2 + i], expected);
        }
        // ShadowCasters carries only A's item.
        CHECK_EQ(shadows[0], (1ull << 32) | 100u);

        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
