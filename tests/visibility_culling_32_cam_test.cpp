// §3.6.5 batch 15b — Boundary test for the 32-camera mask cap.
//
// `DrawItem::cameraMask` is a `uint32_t`, so `cullByFrustum` can
// address at most 32 cameras at once. The Vulkan reference renderer
// (batch 9) plans cascaded shadows that may push 4-8 cameras per
// light; chaining a few lights brings us close to the 32 cap. This
// test pins the contract:
//
//   (1) With exactly 32 cameras, every camera's frustum test
//       contributes a bit; the mask is the union of intersect tests.
//   (2) With 33+ cameras, only the first 32 are addressable; the
//       33rd is silently dropped.
//   (3) Cameras with degenerate (zero-det) projection don't crash
//       the cull and don't put garbage in the mask.
//   (4) `RenderFrame::cameraIndexById` finds each of the 32 by id;
//       returns nullopt beyond.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using namespace threadmaxx;

// Identity-ish projection that admits everything in [-1, 1].
Camera unitOrtho(std::uint32_t id) {
    Camera c;
    c.mode = ProjectionMode::Orthographic;
    c.id = id;
    // Identity view + identity projection means clip = view * world = world
    // and the standard 6 extracted planes are the unit cube.
    return c;
}

} // namespace

int main() {
    // ---- (1) 32 cameras, simple intersect test --------------------
    {
        std::array<Camera, 32> cams;
        for (std::uint32_t i = 0; i < 32; ++i) cams[i] = unitOrtho(i + 1);

        DrawItem item{};
        BoundingVolume bv;
        bv.min = {-0.5f, -0.5f, -0.5f};
        bv.max = { 0.5f,  0.5f,  0.5f};

        std::span<DrawItem> items(&item, 1);
        std::span<const BoundingVolume> bvs(&bv, 1);
        std::span<const Camera> cs(cams.data(), cams.size());
        cullByFrustum(items, bvs, cs);
        // All 32 cameras see the box (they're all the unit cube).
        CHECK_EQ(item.cameraMask, ~0u);
    }

    // ---- (2) 33 cameras — last one is dropped ---------------------
    {
        std::array<Camera, 33> cams;
        for (std::uint32_t i = 0; i < 33; ++i) cams[i] = unitOrtho(i + 1);

        DrawItem item{};
        BoundingVolume bv;
        bv.min = {-0.5f, -0.5f, -0.5f};
        bv.max = { 0.5f,  0.5f,  0.5f};

        std::span<DrawItem> items(&item, 1);
        std::span<const BoundingVolume> bvs(&bv, 1);
        std::span<const Camera> cs(cams.data(), cams.size());
        cullByFrustum(items, bvs, cs);
        // Only the first 32 contribute.
        CHECK_EQ(item.cameraMask, ~0u);  // 32 bits all set
        // The 33rd should not have shifted the mask further (no
        // bit 32 in a uint32_t).
    }

    // ---- (3) Degenerate projection — engine returns "always inside"
    {
        Camera c{};
        c.id = 99;
        // Zero everything in the matrices.
        c.view = {};
        c.projection = {};
        Frustum f = extractFrustum(c);
        BoundingVolume bv;
        bv.min = {-10.0f, -10.0f, -10.0f};
        bv.max = { 10.0f,  10.0f,  10.0f};
        // Engine documents degenerate planes as "always inside".
        CHECK(intersectsFrustum(f, bv.min, bv.max));
    }

    // ---- (4) cameraIndexById on a published RenderFrame -----------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);

        class CamPushSystem : public ISystem {
        public:
            const char* name() const noexcept override { return "cam-push"; }
            ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
            ComponentSet writes() const noexcept override { return ComponentSet::none(); }
            void update(SystemContext&) override {}
            void buildRenderFrame(RenderFrameBuilder& b) override {
                for (std::uint32_t i = 0; i < 32; ++i) {
                    Camera c = unitOrtho(100u + i);
                    b.addCamera(c);
                }
            }
        };

        class CaptureRenderer : public IRenderer {
        public:
            void submitFrame(const RenderFrame& f) override {
                last_ = std::vector<Camera>(f.cameras.begin(), f.cameras.end());
                // Only the frame published after the per-system
                // buildRenderFrame hooks has the 32 cameras — the
                // t=0 frame from initialize() has none. Probe only
                // once we observe a populated cameras span.
                if (f.cameras.size() < 32) return;
                for (std::uint32_t i = 0; i < 32; ++i) {
                    auto idx = f.cameraIndexById(100u + i);
                    CHECK(idx.has_value());
                    CHECK_EQ(*idx, static_cast<std::uint8_t>(i));
                }
                CHECK(!f.cameraIndexById(999u).has_value());
                sawPopulatedFrame_ = true;
            }
            std::vector<Camera> last_;
            bool sawPopulatedFrame_ = false;
        } renderer;
        engine.setRenderer(&renderer);

        struct G : IGame {
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                eng.registerSystem(std::make_unique<CamPushSystem>());
            }
        } g;
        CHECK(engine.initialize(g));
        engine.step();
        CHECK_EQ(renderer.last_.size(), std::size_t{32});
        CHECK(renderer.sawPopulatedFrame_);
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
