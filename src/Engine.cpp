#include "threadmaxx/Engine.hpp"

#include "EngineImpl.hpp"
#include "WorldImpl.hpp"
#include "EntityStorage.hpp"
#include "threadmaxx/Trace.hpp"

#include <algorithm>

namespace threadmaxx {

Engine::Engine(const Config& cfg) : impl_(std::make_unique<internal::EngineImpl>(cfg)) {}
Engine::~Engine() = default;

bool Engine::initialize(IGame& game) { return impl_->initialize(game, *this); }
void Engine::step()                   { impl_->step(); }
void Engine::run()                    { impl_->run(); }
void Engine::shutdown()               { impl_->shutdown(); }

void Engine::requestQuit() noexcept       { impl_->requestQuit(); }
bool Engine::quitRequested() const noexcept { return impl_->quitRequested(); }

void Engine::registerSystem(std::unique_ptr<ISystem> system) {
    impl_->registerSystem(std::move(system));
}
std::size_t Engine::registerSystemAt(std::size_t position,
                                     std::unique_ptr<ISystem> system) {
    return impl_->registerSystemAt(position, std::move(system));
}
std::size_t Engine::registeredSystemCount() const noexcept {
    return impl_->registeredSystemCount();
}
void Engine::setRenderer(IRenderer* r) noexcept { impl_->setRenderer(r); }
void Engine::setLogger(ILogger* l) noexcept     { impl_->setLogger(l); }
ILogger& Engine::logger() const noexcept        { return impl_->logger(); }

World&       Engine::world()       noexcept { return impl_->world(); }
const World& Engine::world() const noexcept { return impl_->world(); }
const Config& Engine::config() const noexcept { return impl_->config(); }

std::uint64_t Engine::tick() const noexcept { return impl_->tick(); }
double Engine::simulationTime() const noexcept { return impl_->simulationTime(); }

EngineStats Engine::stats() const noexcept { return impl_->stats(); }
std::span<const SystemStats> Engine::systemStats() const noexcept {
    return impl_->systemStats();
}

ResourceRegistry&       Engine::resources()       noexcept { return impl_->resources(); }
const ResourceRegistry& Engine::resources() const noexcept { return impl_->resources(); }

IResourceLoader* Engine::addResourceLoader(std::unique_ptr<IResourceLoader> loader) {
    return impl_->addResourceLoader(std::move(loader));
}

std::size_t Engine::resourceLoaderCount() const noexcept {
    return impl_->resourceLoaderCount();
}

LoaderStats Engine::aggregateLoaderStats() const noexcept {
    return impl_->aggregateLoaderStats();
}

void Engine::markResourceStaleRaw_(std::uint32_t index,
                                   std::uint32_t generation,
                                   std::type_index type) {
    impl_->markResourceStale(index, generation, type);
}

bool Engine::preloadUntil(std::function<bool()> done,
                          std::chrono::milliseconds timeout) {
    return impl_->preloadUntil(std::move(done), timeout);
}

JobSystemStats Engine::jobSystemStats() const noexcept { return impl_->jobSystemStats(); }

FrameSnapshot Engine::frameSnapshot() const noexcept {
    return FrameSnapshot{
        impl_->stats(),
        impl_->systemStats(),
        impl_->jobSystemStats(),
    };
}

EntityHandle Engine::reserveEntityHandle() {
    return impl_->world().impl_().storage.reserveHandle();
}
std::uint32_t Engine::reserveEntityHandles(std::uint32_t count,
                                           std::span<EntityHandle> out) {
    const std::uint32_t n = std::min(count,
        static_cast<std::uint32_t>(out.size()));
    impl_->world().impl_().storage.reserveHandles(n, out);
    return n;
}

void Engine::setTimeScale(double s) noexcept { impl_->setTimeScale(s); }
double Engine::timeScale() const noexcept     { return impl_->timeScale(); }
void Engine::setPaused(bool p) noexcept       { impl_->setPaused(p); }
bool Engine::paused() const noexcept          { return impl_->paused(); }

void* Engine::getEventChannelRaw(std::type_index type,
                                 void* (*factory)(),
                                 void (*deleter)(void*),
                                 void (*drainFn)(void*)) {
    return impl_->getEventChannelRaw(type, factory, deleter, drainFn);
}

} // namespace threadmaxx
