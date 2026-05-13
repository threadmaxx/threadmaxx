// §3.4 SpatialHash<Payload>: uniform-grid spatial index.
// Verifies:
//   - insert/clear/size accounting
//   - forEachInRadius returns exactly the entries within the sphere
//   - forEachInBox returns exactly the entries within the AABB
//   - clear() preserves bucket allocation (no functional check; just
//     re-use the index after a clear and confirm queries still work)

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <set>

namespace {

bool roughlyEqual(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

} // namespace

int main() {
    using namespace threadmaxx;

    SpatialHash<int> hash(/*cellSize*/ 5.0f);
    CHECK(roughlyEqual(hash.cellSize(), 5.0f));
    CHECK_EQ(hash.size(), std::size_t{0});

    // Insert a cluster of entities.
    hash.insert(Vec3{0.0f, 0.0f, 0.0f}, 1);
    hash.insert(Vec3{2.0f, 0.0f, 0.0f}, 2);
    hash.insert(Vec3{-2.0f, 0.0f, 0.0f}, 3);
    hash.insert(Vec3{0.0f, 4.0f, 0.0f}, 4);
    hash.insert(Vec3{100.0f, 0.0f, 0.0f}, 5);     // far away
    hash.insert(Vec3{0.0f, 0.0f, -3.0f}, 6);

    CHECK_EQ(hash.size(), std::size_t{6});
    CHECK(hash.cellCount() > 0);

    // forEachInRadius: radius 3 around origin captures 1, 2, 3, 6.
    std::set<int> seen;
    hash.forEachInRadius(Vec3{0, 0, 0}, /*radius*/ 3.0f,
        [&](const Vec3&, int p) { seen.insert(p); });
    CHECK_EQ(seen.size(), std::size_t{4});
    CHECK(seen.count(1) == 1);
    CHECK(seen.count(2) == 1);
    CHECK(seen.count(3) == 1);
    CHECK(seen.count(6) == 1);
    CHECK(seen.count(4) == 0);
    CHECK(seen.count(5) == 0);

    // Larger radius (5) brings in entity 4 at y=4.
    seen.clear();
    hash.forEachInRadius(Vec3{0, 0, 0}, /*radius*/ 5.0f,
        [&](const Vec3&, int p) { seen.insert(p); });
    CHECK(seen.count(4) == 1);
    CHECK(seen.count(5) == 0);

    // Zero/negative radius is a no-op.
    int hit = 0;
    hash.forEachInRadius(Vec3{0, 0, 0}, 0.0f,
        [&](const Vec3&, int) { ++hit; });
    CHECK_EQ(hit, 0);

    // forEachInBox: tight AABB around 1 and 2 only.
    seen.clear();
    hash.forEachInBox(Vec3{-0.5f, -0.5f, -0.5f}, Vec3{2.5f, 0.5f, 0.5f},
        [&](const Vec3&, int p) { seen.insert(p); });
    CHECK_EQ(seen.size(), std::size_t{2});
    CHECK(seen.count(1) == 1);
    CHECK(seen.count(2) == 1);

    // clear() + reuse: empty queries, then re-insert and re-query.
    hash.clear();
    CHECK_EQ(hash.size(), std::size_t{0});
    seen.clear();
    hash.forEachInRadius(Vec3{0, 0, 0}, 100.0f,
        [&](const Vec3&, int p) { seen.insert(p); });
    CHECK(seen.empty());

    hash.insert(Vec3{1, 1, 1}, 99);
    seen.clear();
    hash.forEachInRadius(Vec3{0, 0, 0}, /*radius*/ 2.0f,
        [&](const Vec3&, int p) { seen.insert(p); });
    CHECK_EQ(seen.size(), std::size_t{1});
    CHECK(seen.count(99) == 1);

    // Engine integration: rebuild in a preStep hook, query in update.
    // Verifies the SpatialHash plays nicely with the wave scheduler.
    struct IndexingSystem : ISystem {
        SpatialHash<EntityHandle>* index;
        int foundCount = 0;
        explicit IndexingSystem(SpatialHash<EntityHandle>* idx) : index(idx) {}
        const char* name() const noexcept override { return "idx"; }
        ComponentSet reads()  const noexcept override { return ComponentSet{Component::Transform}; }
        ComponentSet writes() const noexcept override { return ComponentSet::none(); }
        void preStep(SystemContext& ctx) override {
            index->clear();
            const auto& world = ctx.world();
            const auto entities = world.entities();
            const auto transforms = world.transforms();
            for (std::size_t i = 0; i < entities.size(); ++i) {
                index->insert(transforms[i].position, entities[i]);
            }
        }
        void update(SystemContext& ctx) override {
            ctx.single([this](Range, CommandBuffer&) {
                index->forEachInRadius(Vec3{0, 0, 0}, /*radius*/ 10.0f,
                    [this](const Vec3&, EntityHandle) { ++foundCount; });
            });
        }
    };

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;
    Engine engine(cfg);

    SpatialHash<EntityHandle> worldHash(2.0f);
    auto* idxSys = new IndexingSystem(&worldHash);

    struct Game : IGame {
        IndexingSystem* sys;
        explicit Game(IndexingSystem* s) : sys(s) {}
        void onSetup(Engine& e, World&, CommandBuffer& seed) override {
            e.registerSystem(std::unique_ptr<IndexingSystem>(sys));
            seed.spawn(Transform{Vec3{0, 0, 0}, {}, {1, 1, 1}});
            seed.spawn(Transform{Vec3{1, 1, 1}, {}, {1, 1, 1}});
            seed.spawn(Transform{Vec3{100, 0, 0}, {}, {1, 1, 1}});
        }
    } game(idxSys);

    CHECK(engine.initialize(game));
    engine.step();
    CHECK_EQ(idxSys->foundCount, 2);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
