// §3.10.3 batch 24 (F13) — World::forEachChunkOf regression test.
//
// Spawns entities across 3 archetype shapes:
//   A: Transform                       (default mask via spawn(Transform))
//   B: Transform + RenderTag           (spawn with explicit RenderTag)
//   C: Transform + Velocity + Health   (Bundle with Health bit)
// Then exercises `forEachChunkOf` with several `required` masks and
// asserts the right chunks are visited (visit count + chunk masks).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#include <cstdio>
#include <vector>

namespace {

using namespace threadmaxx;

struct CountingSystem : ISystem {
    int fired = 0;
    const char* name() const noexcept override { return "counting"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext&) override { ++fired; }
};

struct SeedGame : IGame {
    void onSetup(Engine&, World&, CommandBuffer& seed) override {
        // Archetype A: just Transform (5 entities).
        for (int i = 0; i < 5; ++i) {
            seed.spawn(Transform{});
        }
        // Archetype B: Transform + RenderTag (3 entities). RenderTag
        // presence is auto-derived from `meshId >= 0`.
        for (int i = 0; i < 3; ++i) {
            seed.spawn(Transform{}, Velocity{}, RenderTag{/*mesh=*/0});
        }
        // Archetype C: Transform + Velocity + Health (2 entities).
        for (int i = 0; i < 2; ++i) {
            Bundle b{};
            b.transform.position = Vec3{float(i), 0, 0};
            b.velocity = Velocity{};
            b.health = Health{50.0f, 100.0f};
            b.initialMask = ComponentSet{Component::Transform,
                                         Component::Velocity,
                                         Component::Health};
            seed.spawnBundle(b);
        }
    }
};

} // namespace

int main() {
    Config cfg; cfg.sleepToPace = false;
    Engine engine(cfg);
    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();   // commit the seed

    const World& w = engine.world();

    // Visit every non-empty chunk: required = none(). Should visit
    // exactly the 3 live archetypes (5+3+2 = 10 entities total). The
    // engine's startup-allocated `all()`-masked empty chunk gets
    // skipped per the F13 contract.
    int allChunks = 0;
    std::uint32_t totalRows = 0;
    w.forEachChunkOf(ComponentSet::none(),
        [&](const internal::ArchetypeChunk& chunk) {
            ++allChunks;
            totalRows += static_cast<std::uint32_t>(chunk.entities.size());
        });
    CHECK_EQ(allChunks, 3);
    CHECK_EQ(totalRows, 10u);     // 5 + 3 + 2
    std::printf("[forEachChunkOf] all non-empty chunks: visited=%d total_rows=%u\n",
                allChunks, totalRows);

    // Only chunks with RenderTag → just archetype B (3 entities).
    int rtChunks = 0;
    std::uint32_t rtRows = 0;
    w.forEachChunkOf(ComponentSet{Component::RenderTag},
        [&](const internal::ArchetypeChunk& chunk) {
            ++rtChunks;
            rtRows += static_cast<std::uint32_t>(chunk.entities.size());
            CHECK(chunk.mask.has(Component::RenderTag));
        });
    CHECK_EQ(rtChunks, 1);
    CHECK_EQ(rtRows, 3u);
    std::printf("[forEachChunkOf] RenderTag: chunks=%d rows=%u\n",
                rtChunks, rtRows);

    // Only chunks with Health → just archetype C.
    int hpChunks = 0;
    std::uint32_t hpRows = 0;
    w.forEachChunkOf(ComponentSet{Component::Health},
        [&](const internal::ArchetypeChunk& chunk) {
            ++hpChunks;
            hpRows += static_cast<std::uint32_t>(chunk.entities.size());
            CHECK(chunk.mask.has(Component::Health));
        });
    CHECK_EQ(hpRows, 2u);

    // Compound mask Transform + Velocity matches ALL 3 archetypes —
    // the default spawn mask in `CommandBuffer::spawn(Transform{})`
    // includes Velocity (along with UserData + Acceleration), so
    // archetype A has it too. Total rows = 10.
    int tvChunks = 0;
    std::uint32_t tvRows = 0;
    w.forEachChunkOf(ComponentSet{Component::Transform, Component::Velocity},
        [&](const internal::ArchetypeChunk& chunk) {
            ++tvChunks;
            tvRows += static_cast<std::uint32_t>(chunk.entities.size());
            CHECK(chunk.mask.hasAll(ComponentSet{
                Component::Transform, Component::Velocity}));
        });
    CHECK_EQ(tvChunks, 3);
    CHECK_EQ(tvRows, 10u);
    std::printf("[forEachChunkOf] Transform+Velocity: chunks=%d rows=%u\n",
                tvChunks, tvRows);

    // No-match mask: a bit not used anywhere. Visit count = 0.
    int noMatch = 0;
    w.forEachChunkOf(ComponentSet{Component::Faction},
        [&](const internal::ArchetypeChunk&) { ++noMatch; });
    CHECK_EQ(noMatch, 0);
    std::printf("[forEachChunkOf] no-match: visited=%d\n", noMatch);

    // Sanity: iteration order matches archetypeChunk(i) for i=0..count-1,
    // modulo the empty-chunk skip.
    std::vector<const internal::ArchetypeChunk*> sequentialNonEmpty;
    for (std::size_t i = 0; i < w.archetypeChunkCount(); ++i) {
        const auto& c = w.archetypeChunk(i);
        if (!c.entities.empty()) sequentialNonEmpty.push_back(&c);
    }
    std::vector<const internal::ArchetypeChunk*> visited;
    w.forEachChunkOf(ComponentSet::none(),
        [&](const internal::ArchetypeChunk& chunk) {
            visited.push_back(&chunk);
        });
    CHECK_EQ(visited.size(), sequentialNonEmpty.size());
    for (std::size_t i = 0; i < visited.size(); ++i) {
        CHECK_EQ(visited[i], sequentialNonEmpty[i]);
    }
    std::printf("[forEachChunkOf] iteration order stable\n");

    EXIT_WITH_RESULT();
}
