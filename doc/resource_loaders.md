# Resource loaders

@page resource_loaders Resource loaders

`IResourceLoader` is the engine's per-tick pump for asset I/O. The
engine does NOT spawn threads for loaders; it calls
`loader->update(engine)` once per `step()` on the simulation thread,
right after the last `postStep` hook commits. The loader owns its own
async pool (or none) and calls `engine.resources().add(...)` when an
asset finishes loading.

```cpp
class MeshLoader : public threadmaxx::IResourceLoader {
public:
    void update(threadmaxx::Engine& engine) override {
        // Drain whatever your I/O thread has produced.
        while (auto ready = ioQueue_.tryPop()) {
            engine.resources().add(std::move(*ready));
        }
    }
private:
    MyIoQueue ioQueue_;
};

// In IGame::onSetup:
engine.addResourceLoader(std::make_unique<MeshLoader>());
```

## Why a sim-thread pump?

The engine keeps the renderer-agnostic, asset-format-agnostic surface
that the project has converged on. Spawning loader threads inside the
engine would require committing to one I/O model (thread pool? coroutine
runtime? io_uring?); pumping `update` instead lets each game pick.

## Lifecycle

Each tick, on the sim thread, every registered loader has two hooks
called back-to-back in registration order:

1. `cancel(Engine&)` — drop newly-stale pending requests. New in
   §3.5 batch 12; defaults to a no-op. Game code that wants to say
   "I no longer need any mesh load for chunk X" pushes the request
   into a loader-internal queue via a loader-specific API, then the
   engine's per-tick `cancel()` pump gives the loader a stable place
   to drain that queue. Increment `LoaderStats::cancelled` per item
   dropped — the engine sums it across loaders in
   `aggregateLoaderStats()`.
2. `update(Engine&)` — poll completed work, install assets via
   `engine.resources().add(...)`, kick off new fetches.

The order is deliberate: dropping stale items before kicking new ones
lets a loader compute "queue is empty after this cancel, idle the
worker thread" in the same tick.

`Engine::shutdown()` calls each loader's `onShutdown(engine)` in
reverse-registration order, then destroys them in the same order.
Use `onShutdown` to cancel in-flight uploads or join I/O threads; the
engine guarantees neither `cancel` nor `update` is invoked again
afterward.

A loader that calls `engine.shutdown()` from inside `update()` is
undefined behavior; treat the pump as a leaf call.

```cpp
class MeshLoader : public threadmaxx::IResourceLoader {
public:
    void onShutdown(threadmaxx::Engine&) override {
        ioPool_.requestStop();
        ioPool_.join();  // safe to block — runs once, during shutdown
    }
    // ...
};
```

## Multi-stage pipelines

A typical loader runs three stages off the sim thread — *fetch* (disk /
network), *decode* (parse / decompress), *upload* (GPU buffer) — and
hands the final asset to the registry via `update()`. The engine
deliberately doesn't model the stages; they live inside the loader. A
typical layout:

```cpp
class TextureLoader : public threadmaxx::IResourceLoader {
public:
    void request(std::string path) { /* enqueue for fetchPool_ */ }

    void update(threadmaxx::Engine& engine) override {
        // Pickup whatever finished since the last tick. Drain in
        // priority order so latency-critical assets land first.
        while (auto t = ready_.tryPop()) {
            const auto id = engine.resources().add(std::move(*t));
            // notify the rest of the game via an event channel
            engine.events<TextureReady>().emit(TextureReady{id});
        }
    }

    threadmaxx::LoaderStats stats() const noexcept override {
        return {
            .pendingLoads    = fetchPool_.queueDepth(),
            .inFlight        = decodePool_.queueDepth() + uploadPool_.queueDepth(),
            .ready           = ready_.size(),
            .failed          = failed_.load(),
            .memoryFootprint = resident_.load(),
            .memoryBudget    = budget_,
        };
    }

    void onShutdown(threadmaxx::Engine&) override {
        fetchPool_.requestStop();  decodePool_.requestStop();  uploadPool_.requestStop();
        fetchPool_.join();         decodePool_.join();         uploadPool_.join();
    }

private:
    /* fetchPool_, decodePool_, uploadPool_, ready_, ... */
};
```

The `stats()` override is optional; the default returns zero
counters. `Engine::aggregateLoaderStats()` sums them across loaders for
HUD readouts:

```cpp
const auto agg = engine.aggregateLoaderStats();
hud.write("loader queue: {}\nresident: {} / {} bytes",
          agg.pendingLoads + agg.inFlight,
          agg.memoryFootprint, agg.memoryBudget);
```

## Hot reload

Override `IResourceLoader::markStale` to receive type-erased
notifications when game code (or a file watcher) decides a previously-
installed asset should be reloaded:

```cpp
void markStale(std::uint32_t index, std::uint32_t generation,
               std::type_index type) override {
    if (type != std::type_index(typeid(Mesh))) return;
    pendingReload_.push_back({index, generation});
}
```

Game code triggers reload via the typed dispatcher:

```cpp
engine.markResourceStale(myMeshId);
```

On the next `update()` pump, the loader installs the new value via
`engine.resources().add(...)` and publishes an `AssetReloaded` event:

```cpp
void update(threadmaxx::Engine& engine) override {
    for (auto stale : pendingReload_) {
        const auto newId = engine.resources().add(reloadFromDisk(stale));
        engine.events<threadmaxx::AssetReloaded>().emit({
            stale.first, stale.second,
            newId.index, newId.generation,
            std::type_index(typeid(Mesh)),
        });
        engine.resources().remove(ResourceId<Mesh>{stale.first, stale.second});
    }
    pendingReload_.clear();
}
```

Subscribers (renderer, gameplay code) rewire their cached ids by
subscribing to `engine.events<AssetReloaded>()`:

```cpp
auto sub = engine.events<AssetReloaded>().subscribeScoped(
    [&](const AssetReloaded& ev) {
        if (ev.matches(currentMeshId_)) {
            currentMeshId_ = ResourceId<Mesh>{ev.newIndex, ev.newGeneration};
        }
    });
```

The library deliberately doesn't auto-redirect `ResourceId`s across
reload; that would require a trampoline layer with its own cost. Each
game decides how to consume the swap (immediately update caches, defer
until the next pass, etc.).

## Boot-time blocking preload

For splash-screen / startup flows where the game shouldn't enter its
main loop until a named set of assets is ready, use
`Engine::preloadUntil`:

```cpp
// kick the loader once to enqueue all needed assets
loader.request("hero.mesh");
loader.request("hero.tex");

const bool ok = engine.preloadUntil(
    [&] {
        return engine.resources().get(heroMeshId) != nullptr &&
               engine.resources().get(heroTexId)  != nullptr;
    },
    std::chrono::milliseconds(5000));
if (!ok) {
    // timed out — bail or fall back to placeholders
}
```

`preloadUntil` pumps loaders synchronously (`update()` only) without
running waves, drains, or advancing the tick. It yields between
iterations so off-thread loader work can progress. Returns true if the
predicate became true within the timeout, false otherwise.

## Cost when there are no loaders

Empty loader list → one `if (resourceLoaders_.empty())`-equivalent
branch per tick. Negligible.
