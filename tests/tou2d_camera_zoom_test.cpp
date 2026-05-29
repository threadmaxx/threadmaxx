// tou2d_camera_zoom_test — pins the M5.7 per-viewport zoom contract.
//
// Contract:
//   * `CameraSystem::effectiveOrthoHalfH() == orthoHalfH() * viewportFor(0).height`
//     across every layout.
//   * The world-units-per-pixel ratio is IDENTICAL across all layouts
//     (1 / 2 / 3 / 4 humans) so a ship sprite renders at the same
//     on-screen pixel size in single-player full-screen and in any
//     split-screen mode. This is the user-visible promise behind the
//     batch — without it, 4-player views shrink ships to half size.
//   * Slot >= numHumans returns a zero-area viewport (cull / no
//     render) but `effectiveOrthoHalfH` falls back to the raw value
//     (defensive — a stray host helper that asks before configuring
//     slots gets a sane number rather than 0).

#include "Check.hpp"

#include "../examples/tou2d/CameraSystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"

#include <cmath>
#include <cstdint>

namespace {

float worldUnitsPerPixel(const tou2d::CameraSystem& cam,
                         std::uint32_t fbH,
                         std::uint8_t slot) {
    const threadmaxx::Viewport vp = cam.viewportFor(slot);
    if (vp.height <= 0.0f) return 0.0f;
    const float pxH = vp.height * static_cast<float>(fbH);
    const float wuH = 2.0f * cam.effectiveOrthoHalfH();
    return wuH / (pxH > 0.0f ? pxH : 1.0f);
}

} // namespace

int main() {
    tou2d::UserComponentIds ids{};

    constexpr std::uint32_t fbW = 1280;
    constexpr std::uint32_t fbH = 720;

    // ---- 1 human: full-screen, scale = 1.0 ------------------------------
    {
        tou2d::CameraSystem cam(ids);
        cam.setViewport(fbW, fbH);
        cam.setNumHumans(1);
        const auto vp = cam.viewportFor(0);
        CHECK_EQ(vp.x, 0.0f);
        CHECK_EQ(vp.y, 0.0f);
        CHECK_EQ(vp.width,  1.0f);
        CHECK_EQ(vp.height, 1.0f);
        CHECK_EQ(cam.effectiveOrthoHalfH(), cam.orthoHalfH());
    }

    // ---- 2 humans: side-by-side, full height, scale = 1.0 ----------------
    {
        tou2d::CameraSystem cam(ids);
        cam.setViewport(fbW, fbH);
        cam.setNumHumans(2);
        const auto vp0 = cam.viewportFor(0);
        const auto vp1 = cam.viewportFor(1);
        CHECK_EQ(vp0.x, 0.0f);
        CHECK_EQ(vp1.x, 0.5f);
        CHECK_EQ(vp0.width,  0.5f);
        CHECK_EQ(vp1.width,  0.5f);
        CHECK_EQ(vp0.height, 1.0f);
        CHECK_EQ(vp1.height, 1.0f);
        CHECK_EQ(cam.effectiveOrthoHalfH(), cam.orthoHalfH());
    }

    // ---- 3 humans: 2x2 grid (slot 3 culled), scale = 0.5 -----------------
    {
        tou2d::CameraSystem cam(ids);
        cam.setViewport(fbW, fbH);
        cam.setNumHumans(3);
        const auto vp0 = cam.viewportFor(0);
        const auto vp2 = cam.viewportFor(2);
        const auto vp3 = cam.viewportFor(3);  // out of range — zero area
        CHECK_EQ(vp0.width,  0.5f);
        CHECK_EQ(vp0.height, 0.5f);
        CHECK_EQ(vp2.height, 0.5f);
        CHECK_EQ(vp3.width,  0.0f);
        CHECK_EQ(vp3.height, 0.0f);
        // Effective half = raw * 0.5
        const float exp = cam.orthoHalfH() * 0.5f;
        CHECK(std::fabs(cam.effectiveOrthoHalfH() - exp) < 1e-4f);
    }

    // ---- 4 humans: 2x2 grid, scale = 0.5 ---------------------------------
    {
        tou2d::CameraSystem cam(ids);
        cam.setViewport(fbW, fbH);
        cam.setNumHumans(4);
        const auto vp0 = cam.viewportFor(0);
        const auto vp3 = cam.viewportFor(3);
        CHECK_EQ(vp0.height, 0.5f);
        CHECK_EQ(vp3.x, 0.5f);
        CHECK_EQ(vp3.y, 0.5f);
        CHECK(vp3.width  > 0.0f);
        CHECK(vp3.height > 0.0f);
        const float exp = cam.orthoHalfH() * 0.5f;
        CHECK(std::fabs(cam.effectiveOrthoHalfH() - exp) < 1e-4f);
    }

    // ---- THE invariant: wu/pixel identical across every layout -----------
    // This is the user-visible promise: a ship's pixel footprint is the
    // same in single-player full-screen and in any split-screen mode.
    float ratio1 = 0.0f, ratio2 = 0.0f, ratio3 = 0.0f, ratio4 = 0.0f;
    {
        tou2d::CameraSystem cam(ids);
        cam.setViewport(fbW, fbH);
        cam.setNumHumans(1);
        ratio1 = worldUnitsPerPixel(cam, fbH, 0);
    }
    {
        tou2d::CameraSystem cam(ids);
        cam.setViewport(fbW, fbH);
        cam.setNumHumans(2);
        ratio2 = worldUnitsPerPixel(cam, fbH, 0);
    }
    {
        tou2d::CameraSystem cam(ids);
        cam.setViewport(fbW, fbH);
        cam.setNumHumans(3);
        ratio3 = worldUnitsPerPixel(cam, fbH, 0);
    }
    {
        tou2d::CameraSystem cam(ids);
        cam.setViewport(fbW, fbH);
        cam.setNumHumans(4);
        ratio4 = worldUnitsPerPixel(cam, fbH, 0);
    }
    CHECK(ratio1 > 0.0f);
    CHECK(std::fabs(ratio1 - ratio2) < 1e-4f);
    CHECK(std::fabs(ratio1 - ratio3) < 1e-4f);
    CHECK(std::fabs(ratio1 - ratio4) < 1e-4f);

    EXIT_WITH_RESULT();
}
