// §3.11 batch D-audit — regression test for the input-edge bug.
//
// The bug fixed in batch D-audit: `PlayerInputSystem` used to call
// `takeEdges()` which CLEARS THE WHOLE BITMASK, so when the player
// pressed F (attack) on the same tick as F5 (save) or F1 (trace),
// the non-attack edges were silently consumed and lost.
//
// This test injects all four edges simultaneously, steps once, and
// verifies each system consumed only its own bit. Specifically:
//   - kEdgeAttack should be cleared (PlayerInputSystem).
//   - kEdgeSaveQuick should be cleared (SaveLoadSystem).
//   - kEdgeTrace should be cleared (HudSystem).
//   - kEdgeLoadQuick should be cleared (SaveLoadSystem).
//
// All four edges fire on the same tick. Pre-fix this test would have
// failed: PlayerInputSystem (runs first) would have eaten all of them
// and the save/load/trace systems would have seen edges=0.

#include "DemoTestHarness.hpp"

#include <cstdio>

int main() {
    using namespace rpg;
    using namespace rpg::testing;

    resetEdges();
    auto fx = makeHeadless();
    fx.engine->step();   // commit seed

    injectEdge(kEdgeAttack);
    injectEdge(kEdgeSaveQuick);
    injectEdge(kEdgeTrace);
    // Don't inject Load — that would tear down the world mid-test.

    fx.engine->step();   // every system gets one preStep + update

    const std::uint32_t remaining =
        input().edges.load(std::memory_order_acquire);
    std::printf("[test_input_edges] remaining edges = 0x%x\n", remaining);
    // The three injected edges should all have been consumed by their
    // owning systems. None should leak forward into the next tick.
    CHECK_EQ(remaining & kEdgeAttack,    0u);
    CHECK_EQ(remaining & kEdgeSaveQuick, 0u);
    CHECK_EQ(remaining & kEdgeTrace,     0u);
    EXIT_WITH_RESULT();
}
