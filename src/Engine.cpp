#include "threadmaxx/Engine.hpp"

#include "EngineImpl.hpp"

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
void Engine::setRenderer(IRenderer* r) noexcept { impl_->setRenderer(r); }

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

} // namespace threadmaxx
