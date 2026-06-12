#pragma once

// Opt-in: only compiled when threadmaxx::threadmaxx is linked into the
// assets target. The build system sets THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE=1
// when this header is reachable, mirroring the input lib's UI-bridge gate.

#if THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE

#include <threadmaxx/Resource.hpp>

#include "async_loader.hpp"
#include "registry.hpp"

namespace threadmaxx::assets {

// IResourceLoader adapter. Hand to Engine::addResourceLoader and the
// engine will pump the AsyncLoader once per `step()`. Hot-reload signals
// can be routed through Engine::markResourceStale → markStale.
class EngineAssetLoader : public threadmaxx::IResourceLoader {
public:
    EngineAssetLoader(AssetRegistry& /*registry*/, AsyncLoader& loader) noexcept
        : loader_(loader) {}

    void update(threadmaxx::Engine& /*engine*/) override {
        ++updateCalls_;
        loader_.pump();
    }
    std::uint64_t updateCalls() const noexcept { return updateCalls_; }

    threadmaxx::LoaderStats stats() const noexcept override {
        threadmaxx::LoaderStats s{};
        s.pendingLoads = static_cast<std::uint64_t>(loader_.pendingCount());
        s.inFlight     = static_cast<std::uint64_t>(loader_.inFlightCount());
        s.failed       = static_cast<std::uint64_t>(loader_.failedCount());
        s.ready        = 0;
        return s;
    }

private:
    AsyncLoader&   loader_;
    std::uint64_t  updateCalls_{0};
};

} // namespace threadmaxx::assets

#endif // THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE
