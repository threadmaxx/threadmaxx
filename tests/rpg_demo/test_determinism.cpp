// §3.11.4 batch D4 — scripted-replay determinism regression test.
//
// Boots the demo twice with an identical sequence of input edges
// injected at identical ticks, captures `EngineStats::commitHash` per
// tick on each run, and asserts the per-tick hashes match. This is
// the demo's regression test bed for combat / pickup / save-load
// determinism — if any system gains a hidden non-determinism source
// (e.g. an unseeded RNG, an unguarded `std::time(nullptr)`, a
// thread-id-dependent code path), this test surfaces it as a
// first-tick mismatch.
//
// The "scripted input" sequence is encoded inline here as a fixed
// vector of `(tick, edge_bits)` pairs. Future evolution: serialize
// the same sequence to disk for a `--replay <path>` CLI mode on the
// demo executable. The library currently has no built-in skip-log
// serializer (the FUTURE_WORK §3.11.4 entry mentions it as a
// potential §3.10.3 follow-on); per the conservative-expansion
// policy we leave it out until a demand emerges.

#include "DemoTestHarness.hpp"

#include <threadmaxx/Engine.hpp>

#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using namespace rpg;
using namespace rpg::testing;

struct ScriptedInput {
    std::uint64_t tick;
    std::uint32_t edges;
};

// A handful of attacks at scattered ticks. Deliberately sparse so the
// engine spends most ticks doing normal idle-NPC simulation work —
// catching any non-determinism in the AI loop, not just in input
// consumption.
constexpr ScriptedInput kScript[] = {
    { 3,  kEdgeAttack},
    { 7,  kEdgeAttack},
    {15,  kEdgeAttack},
    {25,  kEdgeAttack},
    {40,  kEdgeAttack},
};
constexpr std::size_t kScriptLen = sizeof(kScript) / sizeof(kScript[0]);
constexpr std::uint64_t kRunTicks = 60;

std::vector<std::uint64_t> runOnce() {
    resetEdges();
    threadmaxx::Config cfg;
    cfg.sleepToPace   = false;
    cfg.workerCount   = 2;
    cfg.deterministic = true;
    threadmaxx::Engine engine(cfg);
    DemoGame game;
    CHECK(engine.initialize(game));

    std::vector<std::uint64_t> hashes;
    hashes.reserve(kRunTicks);
    std::size_t nextScript = 0;
    for (std::uint64_t t = 0; t < kRunTicks; ++t) {
        // Inject any scripted edges for this tick BEFORE step() so the
        // sim-thread systems consume them this same tick.
        while (nextScript < kScriptLen && kScript[nextScript].tick == t) {
            injectEdge(kScript[nextScript].edges);
            ++nextScript;
        }
        engine.step();
        hashes.push_back(engine.stats().commitHash);
    }
    return hashes;
}

} // namespace

int main() {
    const auto a = runOnce();
    const auto b = runOnce();
    CHECK_EQ(a.size(), b.size());

    bool anyMismatch = false;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i] != b[i]) {
            std::fprintf(stderr,
                         "[test_determinism] mismatch at tick=%zu  "
                         "run_a=0x%016llx  run_b=0x%016llx\n",
                         i,
                         static_cast<unsigned long long>(a[i]),
                         static_cast<unsigned long long>(b[i]));
            anyMismatch = true;
            break;
        }
    }
    CHECK(!anyMismatch);

    // Sanity: the final hash should be non-trivial (lots of commits
    // happened over 60 ticks of base-scene NPC simulation).
    CHECK(a.back() != 0xcbf29ce484222325ull);  // FNV-1a basis (empty)
    std::printf("[test_determinism] %zu ticks identical, "
                "final hash=0x%016llx\n",
                a.size(),
                static_cast<unsigned long long>(a.back()));
    EXIT_WITH_RESULT();
}
