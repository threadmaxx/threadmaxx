# Spatial hash

@page spatial_hash Spatial hash

`SpatialHash<Payload>` is a uniform-grid index for neighbor lookups,
broadphase queries, AOI for streaming, range-based AI targeting — any
case that would otherwise require an `O(n²)` scan over entities.

```cpp
threadmaxx::SpatialHash<threadmaxx::EntityHandle> hash(/*cellSize*/ 5.0f);
hash.clear();
for (auto e : world.entities()) {
    hash.insert(world.get<Transform>(e).position, e);
}

hash.forEachInRadius(player.position, /*radius*/ 8.0f,
    [&](const Vec3& pos, EntityHandle other) {
        // ...
    });
```

The engine does not own the index — pick where to rebuild it. The most
common pattern is in a `preStep` hook on a small built-in system, so
wave systems see the just-rebuilt index:

```cpp
class IndexingSystem : public threadmaxx::ISystem {
public:
    threadmaxx::SpatialHash<threadmaxx::EntityHandle>* index;
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::Component::Transform;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    void preStep(threadmaxx::SystemContext& ctx) override {
        index->clear();
        const auto& w = ctx.world();
        const auto entities = w.entities();
        const auto transforms = w.transforms();
        for (std::size_t i = 0; i < entities.size(); ++i) {
            index->insert(transforms[i].position, entities[i]);
        }
    }
    void update(threadmaxx::SystemContext&) override {}
};
```

## Cell size

Pick a cell size near your typical query radius. Too small → each
query walks many cells. Too large → each cell holds many candidates and
the per-entity exact distance test does more work.

`cellCount()` and `size()` let you measure the trade-off — a healthy
grid has a small handful of entries per populated cell on average.

## Query shapes

- `forEachInRadius(center, radius, fn)` — full 3D Euclidean distance.
  Returns exactly the entries inside the sphere.
- `forEachInBox(min, max, fn)` — axis-aligned box.

Both invoke `fn(const Vec3& position, const Payload&)`. Captures by
value or reference behave normally.

## Allocations

`clear()` keeps the bucket allocation alive. Steady-state per-tick
rebuilds pay zero allocations after the first tick. The internal
storage is a flat `std::unordered_map` keyed by cell coordinate; if you
have a very large world, profile and consider a flat hash variant from
your favorite container library — the storage is internal and a
drop-in replacement does not change the public API.

## Thread safety

`SpatialHash` is NOT thread-safe. Insert from a single thread (e.g.
the `preStep` hook running on the sim thread). Queries are safe to do
from worker jobs once the index is built and no other thread is
mutating it — the same rule that applies to `const World&`.
