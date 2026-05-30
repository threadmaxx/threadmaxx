// tou2d_debug_overlay_test — M6.9 contract pin for the F3-toggled
// debug / benchmark overlay.
//
// Pins:
//   * Toggle round-trip — default off; `toggle()` flips; `setVisible`
//     overrides; visible() reports the truth.
//   * Invisible → buildRenderFrame emits ZERO debug primitives (no
//     text / lines / points / draw items). Zero-cost when off.
//   * Visible with no engine + no test snapshot → buildRenderFrame is
//     still safe (no crash, no output). The system tolerates both
//     producers being absent.
//   * Visible with an injected test snapshot — at least the fixed
//     rows fire (FPS / tick-hash / entities / commit-render breakdown)
//     and the chosen substrings appear verbatim in the emitted text.
//   * Top-N systems row — when three systems are passed in, the
//     emitted top-3 rows name them in DESCENDING `avgUpdateSeconds`
//     order, regardless of registration order.
//   * Hash row formats the low 32 bits of `commitHash` as 8 hex chars.

#include "Check.hpp"

#include "../examples/tou2d/CameraSystem.hpp"
#include "../examples/tou2d/DebugOverlaySystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"

#include <threadmaxx/Stats.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace {

bool anyTextContains(const threadmaxx::RenderFrameBuilder& b,
                     std::string_view needle) {
    for (const auto& t : b.debugText()) {
        if (t.text.find(needle) != std::string_view::npos) return true;
    }
    return false;
}

const threadmaxx::DebugText* findContaining(
    const threadmaxx::RenderFrameBuilder& b,
    std::string_view needle) {
    for (const auto& t : b.debugText()) {
        if (t.text.find(needle) != std::string_view::npos) return &t;
    }
    return nullptr;
}

} // namespace

int main() {
    using tou2d::CameraSystem;
    using tou2d::DebugOverlaySystem;
    using tou2d::UserComponentIds;

    const UserComponentIds ids{};

    // ---- 1. Toggle round-trip --------------------------------------
    {
        CameraSystem cam(ids);
        DebugOverlaySystem dbg(/*engine=*/nullptr, &cam);
        CHECK(!dbg.visible());                 // default off
        dbg.toggle();
        CHECK(dbg.visible());
        dbg.toggle();
        CHECK(!dbg.visible());
        dbg.setVisible(true);
        CHECK(dbg.visible());
        dbg.setVisible(false);
        CHECK(!dbg.visible());
    }

    // ---- 2. Invisible → no debug primitives emitted ----------------
    {
        CameraSystem cam(ids);
        DebugOverlaySystem dbg(/*engine=*/nullptr, &cam);
        // Even with a snapshot injected the off state stays mute.
        threadmaxx::EngineStats es{};
        es.tick = 42;
        es.commitHash = 0xCAFEBABEDEADBEEFull;
        dbg.setTestSnapshot(es, {}, {});

        threadmaxx::RenderFrameBuilder b;
        dbg.buildRenderFrame(b);

        CHECK_EQ(b.debugText().size(),   std::size_t{0});
        CHECK_EQ(b.debugLines().size(),  std::size_t{0});
        CHECK_EQ(b.debugPoints().size(), std::size_t{0});
    }

    // ---- 3. Visible without snapshot or engine → safe no-output ----
    {
        // No engine pointer and no test snapshot → must not crash,
        // must emit nothing. (Same posture as a renderer that opens
        // the overlay before the engine is wired.)
        DebugOverlaySystem dbg(/*engine=*/nullptr, /*camera=*/nullptr);
        dbg.setVisible(true);
        threadmaxx::RenderFrameBuilder b;
        dbg.buildRenderFrame(b);
        CHECK_EQ(b.debugText().size(), std::size_t{0});
    }

    // ---- 4. Visible with test snapshot — fixed rows fire -----------
    {
        CameraSystem cam(ids);
        cam.setNumHumans(2);
        DebugOverlaySystem dbg(/*engine=*/nullptr, &cam);
        dbg.setVisible(true);

        threadmaxx::EngineStats es{};
        es.tick                          = 12345;
        es.commitHash                    = 0x00000000DEADBEEFull;
        es.avgStepSeconds                = 1.0 / 60.0;     // → 60 FPS
        es.aliveEntities                 = 4096;
        es.commandsCommittedLastStep     = 250;
        es.jobsSubmittedLastStep         = 30;
        es.commitDurationSeconds         = 250e-6;          // 250 µs
        es.engineBuildRenderFrameSeconds = 75e-6;           // 75 µs

        threadmaxx::JobSystemStats js{};
        js.workerCount = 8;

        dbg.setTestSnapshot(es, {}, js);

        threadmaxx::RenderFrameBuilder b;
        dbg.buildRenderFrame(b);

        // FPS row formats the avgStepSeconds → 60.0.
        const auto* fps = findContaining(b, "FPS");
        CHECK(fps != nullptr);
        CHECK(fps->text.find("60.0") != std::string_view::npos);
        // ms = 16.67 → "16.67" rounded to "16.67ms".
        CHECK(fps->text.find("16.67ms") != std::string_view::npos);

        // tick / hash / workers row.
        const auto* tk = findContaining(b, "tick=12345");
        CHECK(tk != nullptr);
        // Low 32 bits of 0x00000000DEADBEEF → "deadbeef".
        CHECK(tk->text.find("deadbeef") != std::string_view::npos);
        CHECK(tk->text.find("workers=8") != std::string_view::npos);

        // entities / cmds / jobs row.
        const auto* en = findContaining(b, "entities=4096");
        CHECK(en != nullptr);
        CHECK(en->text.find("cmds/step=250") != std::string_view::npos);
        CHECK(en->text.find("jobs/step=30")  != std::string_view::npos);

        // commit / render breakdown — formatted in microseconds.
        const auto* cr = findContaining(b, "commit=");
        CHECK(cr != nullptr);
        // 250 µs → "250.0us"; 75 µs → "75.0us".
        CHECK(cr->text.find("250.0us") != std::string_view::npos);
        CHECK(cr->text.find("75.0us")  != std::string_view::npos);

        // humans row — sourced from CameraSystem::numHumans.
        CHECK(anyTextContains(b, "humans=2"));
    }

    // ---- 5. Top-N systems — sorted by avgUpdateSeconds DESC --------
    {
        CameraSystem cam(ids);
        DebugOverlaySystem dbg(/*engine=*/nullptr, &cam);
        dbg.setVisible(true);

        threadmaxx::EngineStats es{};
        es.avgStepSeconds = 1.0 / 60.0;
        threadmaxx::JobSystemStats js{};
        js.workerCount = 1;

        // Names are string literals so the SystemStats::name lifetime
        // outlives setTestSnapshot's copy (the stats struct stores the
        // borrowed pointer verbatim).
        threadmaxx::SystemStats a{};
        a.name = "alpha";
        a.avgUpdateSeconds = 50e-6;
        threadmaxx::SystemStats b{};
        b.name = "beta";
        b.avgUpdateSeconds = 300e-6;
        threadmaxx::SystemStats c{};
        c.name = "gamma";
        c.avgUpdateSeconds = 100e-6;
        threadmaxx::SystemStats d{};
        d.name = "delta";
        d.avgUpdateSeconds = 25e-6;
        std::vector<threadmaxx::SystemStats> sys{a, b, c, d};
        dbg.setTestSnapshot(es, sys, js);

        threadmaxx::RenderFrameBuilder rb;
        dbg.buildRenderFrame(rb);

        // Expect rows "top1 beta ...", "top2 gamma ...", "top3 alpha ..."
        const auto* t1 = findContaining(rb, "top1");
        const auto* t2 = findContaining(rb, "top2");
        const auto* t3 = findContaining(rb, "top3");
        CHECK(t1 != nullptr);
        CHECK(t2 != nullptr);
        CHECK(t3 != nullptr);
        CHECK(t1->text.find("beta")  != std::string_view::npos);
        CHECK(t2->text.find("gamma") != std::string_view::npos);
        CHECK(t3->text.find("alpha") != std::string_view::npos);
        // 4th system (delta) should NOT appear (kTopSystemRows = 3).
        CHECK(!anyTextContains(rb, "delta"));
        // The chosen times appear formatted as microseconds.
        CHECK(t1->text.find("300.0us") != std::string_view::npos);
        CHECK(t2->text.find("100.0us") != std::string_view::npos);
        CHECK(t3->text.find("50.0us")  != std::string_view::npos);
    }

    // ---- 6. clearTestSnapshot drops the override -------------------
    {
        // After clearTestSnapshot + no engine_, buildRenderFrame must
        // emit nothing — proves clearTestSnapshot actually disengages
        // the test path (and visibility alone with no producer is a
        // safe no-op).
        DebugOverlaySystem dbg(/*engine=*/nullptr, /*camera=*/nullptr);
        threadmaxx::EngineStats es{};
        es.tick = 7;
        dbg.setTestSnapshot(es, {}, {});
        dbg.clearTestSnapshot();
        dbg.setVisible(true);
        threadmaxx::RenderFrameBuilder rb;
        dbg.buildRenderFrame(rb);
        CHECK_EQ(rb.debugText().size(), std::size_t{0});
    }

    // ---- 7. M6.9b — game-state rows fire only after setGameStats ---
    {
        CameraSystem cam(ids);
        cam.setNumHumans(3);
        DebugOverlaySystem dbg(/*engine=*/nullptr, &cam);
        dbg.setVisible(true);

        threadmaxx::EngineStats es{};
        es.avgStepSeconds = 1.0 / 60.0;
        threadmaxx::JobSystemStats js{};
        js.workerCount = 4;
        dbg.setTestSnapshot(es, {}, js);

        // Without setGameStats: telemetry rows fire, but no game rows.
        {
            threadmaxx::RenderFrameBuilder rb;
            dbg.buildRenderFrame(rb);
            CHECK(anyTextContains(rb, "FPS"));
            CHECK(!anyTextContains(rb, "bullets="));
            CHECK(!anyTextContains(rb, "particles="));
            CHECK(!anyTextContains(rb, "terrain="));
            CHECK(!anyTextContains(rb, "seed="));
            // viewport count is in the same row as humans=; mode label
            // is the new addition — confirm it lands on the humans row.
            CHECK(anyTextContains(rb, "humans=3"));
            CHECK(anyTextContains(rb, "mode=3H"));
        }

        // Push game stats — rows now appear.
        tou2d::DebugGameStats g{};
        g.aliveBullets       = 42;
        g.aliveParticles     = 17;
        g.solidTerrainCells  = 1234;
        g.viewportCount      = 3;
        g.cameraMode         = "3H";
        g.worldSeed          = "gen:0xdeadbeef";
        dbg.setGameStats(g);

        threadmaxx::RenderFrameBuilder rb;
        dbg.buildRenderFrame(rb);
        CHECK(anyTextContains(rb, "bullets=42"));
        CHECK(anyTextContains(rb, "particles=17"));
        CHECK(anyTextContains(rb, "terrain=1234"));
        CHECK(anyTextContains(rb, "viewports=3"));
        CHECK(anyTextContains(rb, "seed=gen:0xdeadbeef"));
    }

    // ---- 8. M6.9b — buildRenderFrame budget row + color flip -------
    {
        CameraSystem cam(ids);
        DebugOverlaySystem dbg(/*engine=*/nullptr, &cam);
        dbg.setVisible(true);

        threadmaxx::EngineStats es{};
        es.avgStepSeconds = 1.0 / 60.0;
        threadmaxx::JobSystemStats js{};
        js.workerCount = 1;

        // Three systems summing to 100 µs — UNDER the 150 µs gate.
        threadmaxx::SystemStats a{}; a.name = "a";
        a.buildRenderFrameSeconds = 30e-6;
        threadmaxx::SystemStats b{}; b.name = "b";
        b.buildRenderFrameSeconds = 40e-6;
        threadmaxx::SystemStats c{}; c.name = "c";
        c.buildRenderFrameSeconds = 30e-6;
        std::vector<threadmaxx::SystemStats> sysUnder{a, b, c};
        dbg.setTestSnapshot(es, sysUnder, js);

        threadmaxx::RenderFrameBuilder rbUnder;
        dbg.buildRenderFrame(rbUnder);
        const auto* rfbU = findContaining(rbUnder, "rfb=");
        CHECK(rfbU != nullptr);
        // Total = 100 µs against 150 µs gate; substring "100.0us" + "150"
        CHECK(rfbU->text.find("100.0us") != std::string_view::npos);
        CHECK(rfbU->text.find("150")     != std::string_view::npos);
        // Under-budget color = kRowColor (pale green 0xFFCCFFCC).
        CHECK_EQ(rfbU->colorRGBA, 0xFFCCFFCCu);

        // Push past the budget: 80 + 80 + 30 = 190 µs.
        a.buildRenderFrameSeconds = 80e-6;
        b.buildRenderFrameSeconds = 80e-6;
        c.buildRenderFrameSeconds = 30e-6;
        std::vector<threadmaxx::SystemStats> sysOver{a, b, c};
        dbg.setTestSnapshot(es, sysOver, js);

        threadmaxx::RenderFrameBuilder rbOver;
        dbg.buildRenderFrame(rbOver);
        const auto* rfbO = findContaining(rbOver, "rfb=");
        CHECK(rfbO != nullptr);
        CHECK(rfbO->text.find("190.0us") != std::string_view::npos);
        // Over-budget color = pale red 0xFFFF6666.
        CHECK_EQ(rfbO->colorRGBA, 0xFFFF6666u);
    }

    EXIT_WITH_RESULT();
}
