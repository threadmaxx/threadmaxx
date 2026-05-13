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

- Loaders are pumped in registration order, every step.
- Loaders are destroyed in reverse-registration order during
  `Engine::shutdown()` — so a loader that depends on another (e.g. a
  texture loader that needs the GPU upload loader registered first)
  tears down second.
- A loader that calls `engine.shutdown()` from inside `update()` is
  undefined behavior; treat the pump as a leaf call.

## Hot reload

Add a `markStale(ResourceId)` method on your loader subclass. When the
file watcher (or your equivalent) notices a change, queue a reload; the
next `update()` pump picks it up. The engine's contract stays the
same — it just keeps pumping `update()`.

## Cost when there are no loaders

Empty loader list → one `if (resourceLoaders_.empty())`-equivalent
branch per tick. Negligible.
