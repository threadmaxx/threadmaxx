/// @file test_studio_remote_command_roundtrip.cpp
/// @brief ST31 — full mutation round-trip: studio → SubmitCommand
/// (label) → agent → factory → editor::CommandStack. Verifies the
/// stack received the command, the agent applied-counter bumped, and
/// the studio-side cache observed the CommandResult.

#include "Check.hpp"

#include <threadmaxx_studio/agent.hpp>
#include <threadmaxx_studio/data_source.hpp>
#include <threadmaxx_studio/remote_data_source.hpp>

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/types.hpp>

#include <threadmaxx_network/transport.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace {

using namespace threadmaxx;

class StubDataSource final : public studio::IStudioDataSource {
public:
    studio::AttachMode mode() const noexcept override {
        return studio::AttachMode::Direct;
    }
    std::optional<studio::EngineFrameSummary>
    engineSnapshot() const override {
        return std::nullopt;
    }
};

struct NoopCommand final : editor::IEditCommand {
    int* counter{nullptr};
    explicit NoopCommand(int* c) : counter(c) {
        if (counter) ++(*counter);
    }
    std::string_view name() const noexcept override { return "Noop"; }
    void apply(threadmaxx::CommandBuffer&) override {}
    void undo(threadmaxx::CommandBuffer&) override {}
};

} // namespace

int main() {
    using namespace threadmaxx;

    // Live engine + stack (the stack registers an editor pump-system
    // on construction, so the engine must outlive the stack).
    Engine engine{Config{}};
    editor::CommandStack stack{engine};

    StubDataSource source;

    auto hub = std::make_shared<network::LoopbackHub>();
    network::LoopbackTransport agentTransport{hub};
    network::LoopbackTransport studioTransport{hub};

    studio::StudioAgent agent{agentTransport, source};
    agent.setAttachEnabled(true);  // ST32 — production gate, on for tests.
    studio::RemoteDataSource remote{studioTransport,
                                    agentTransport.localPeer()};

    // Without a stack bound, mutation requests are rejected.
    CHECK(remote.submitCommand("Translate"));
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(agent.commandsRejected(), 1u);
    CHECK_EQ(agent.commandsApplied(), 0u);
    CHECK_EQ(remote.pump(), 1u);
    CHECK(!remote.lastCommandAccepted());
    CHECK_EQ(remote.commandResponsesReceived(), 1u);

    // Bind the stack + register a factory.
    agent.setCommandStack(&stack);
    int factoryInvocations = 0;
    CHECK(agent.registerCommandFactory(
        "Translate", [&factoryInvocations]() {
            return std::make_unique<NoopCommand>(&factoryInvocations);
        }));
    CHECK_EQ(agent.commandFactoryCount(), 1u);

    // Studio side submits. Pump shuttles the request through.
    CHECK_EQ(stack.historySize(), 0u);
    CHECK(remote.submitCommand("Translate"));
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(agent.commandsApplied(), 1u);
    CHECK_EQ(factoryInvocations, 1);
    CHECK_EQ(stack.historySize(), 1u);

    // Response comes back ok.
    CHECK_EQ(remote.pump(), 1u);
    CHECK(remote.lastCommandAccepted());
    CHECK_EQ(remote.commandResponsesReceived(), 2u);

    // Unknown label → rejected.
    CHECK(remote.submitCommand("Unknown"));
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(agent.commandsApplied(), 1u);    // unchanged
    CHECK_EQ(agent.commandsRejected(), 2u);
    CHECK_EQ(remote.pump(), 1u);
    CHECK(!remote.lastCommandAccepted());

    // Re-registering overwrites the factory (returns false).
    CHECK(!agent.registerCommandFactory(
        "Translate", []() {
            return std::unique_ptr<editor::IEditCommand>{};
        }));
    CHECK_EQ(agent.commandFactoryCount(), 1u);

    // Factory returning nullptr is treated as rejection.
    CHECK(remote.submitCommand("Translate"));
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(agent.commandsApplied(), 1u);
    CHECK_EQ(agent.commandsRejected(), 3u);

    // Detaching the stack returns to the unbound rejection path.
    agent.setCommandStack(nullptr);
    CHECK(remote.submitCommand("Translate"));
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(agent.commandsRejected(), 4u);

    // Critical: engine must shut down before stack falls out of scope
    // (the stack's pump-system was registered on it).
    engine.shutdown();
    EXIT_WITH_RESULT();
}
