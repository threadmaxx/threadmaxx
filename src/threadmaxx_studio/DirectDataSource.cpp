/// @file DirectDataSource.cpp

#include <threadmaxx_studio/direct_data_source.hpp>

#include <threadmaxx_editor/commands.hpp>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Serialization.hpp>
#include <threadmaxx/Trace.hpp>
#include <threadmaxx/World.hpp>

namespace threadmaxx::studio {

DirectDataSource::DirectDataSource(threadmaxx::Engine& engine,
                                   editor::CommandStack& stack) noexcept
    : engine_(&engine), stack_(&stack) {}

std::optional<EngineFrameSummary> DirectDataSource::engineSnapshot() const {
    const auto snap = engine_->frameSnapshot();
    EngineFrameSummary out{};
    out.tick = snap.engine.tick;
    out.lastStepSeconds = snap.engine.lastStepSeconds;
    out.paused = engine_->paused();
    out.systemCount =
        static_cast<std::uint32_t>(engine_->registeredSystemCount());
    out.workerCount = engine_->workerCount();
    return out;
}

threadmaxx::WorldSnapshot DirectDataSource::worldSnapshot() const {
    return engine_->world().snapshot();
}

bool DirectDataSource::submitEditCommand(
    std::unique_ptr<editor::IEditCommand> cmd) {
    if (cmd == nullptr) {
        return false;
    }
    (void)stack_->execute(std::move(cmd));
    return true;
}

} // namespace threadmaxx::studio
