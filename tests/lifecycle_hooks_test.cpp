// §3.1 preStep/postStep hooks: invocation order, registration order across
// systems, commands committed before/after waves.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <string>
#include <vector>

namespace {

// Records the sequence of pre/update/post events into a shared transcript
// so we can assert on the global ordering at the end.
struct Transcript {
    std::vector<std::string> events;
};

class RecordingSystem : public threadmaxx::ISystem {
public:
    RecordingSystem(const char* name, Transcript& t)
        : name_(name), transcript_(t) {}

    const char* name() const noexcept override { return name_; }

    void preStep(threadmaxx::SystemContext&) override {
        transcript_.events.push_back(std::string("pre:") + name_);
    }
    void update(threadmaxx::SystemContext&) override {
        transcript_.events.push_back(std::string("update:") + name_);
    }
    void postStep(threadmaxx::SystemContext&) override {
        transcript_.events.push_back(std::string("post:") + name_);
    }

    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

private:
    const char* name_;
    Transcript& transcript_;
};

// Pump-style preStep: records into UserData of entity 0 so the
// wave-phase update sees it via the read accessor. Verifies that
// preStep commits land before update().
class PrePump : public threadmaxx::ISystem {
public:
    explicit PrePump(threadmaxx::EntityHandle e) : target_(e) {}
    const char* name() const noexcept override { return "prepump"; }

    void preStep(threadmaxx::SystemContext& ctx) override {
        auto t = target_;
        ctx.single([t](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
            cb.setUserData(t, threadmaxx::UserData{42});
        });
    }
    void update(threadmaxx::SystemContext& ctx) override {
        const auto* u = ctx.world().tryGetUserData(target_);
        // The preStep commit must already be visible during update.
        if (u && u->value == 42) sawCorrectValueInUpdate = true;
    }
    void postStep(threadmaxx::SystemContext& ctx) override {
        const auto* u = ctx.world().tryGetUserData(target_);
        if (u) postStepSawValue = u->value;
    }

    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::UserData};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::UserData};
    }

    bool sawCorrectValueInUpdate = false;
    std::uint64_t postStepSawValue = 0;
private:
    threadmaxx::EntityHandle target_;
};

class HookGame : public threadmaxx::IGame {
public:
    HookGame(Transcript& t, threadmaxx::EntityHandle& outE) : t_(t), out_(outE) {}
    void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        out_ = e.reserveEntityHandle();
        seed.spawn(out_, threadmaxx::Transform{});
        // Register A and B; A goes first, B second.
        e.registerSystem(std::make_unique<RecordingSystem>("A", t_));
        e.registerSystem(std::make_unique<RecordingSystem>("B", t_));
    }
private:
    Transcript& t_;
    threadmaxx::EntityHandle& out_;
};

} // namespace

int main() {
    using namespace threadmaxx;

    // Test 1: invocation order across two systems.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine e(cfg);
        Transcript t;
        EntityHandle target{};
        HookGame g(t, target);
        CHECK(e.initialize(g));
        e.step();

        // Expected order across one step:
        //   pre:A, pre:B, update:A, update:B (or B then A — systems with
        //   reads/writes=none share a wave, so update order across them
        //   is not guaranteed), post:A, post:B.
        CHECK_EQ(t.events.size(), std::size_t{6});
        CHECK(t.events[0] == "pre:A");
        CHECK(t.events[1] == "pre:B");
        // Updates can be in either order in the same wave; just check
        // both appear contiguously after the pre-block.
        CHECK((t.events[2] == "update:A" && t.events[3] == "update:B")
           || (t.events[2] == "update:B" && t.events[3] == "update:A"));
        CHECK(t.events[4] == "post:A");
        CHECK(t.events[5] == "post:B");

        e.shutdown();
    }

    // Test 2: preStep commits are visible during update; postStep sees
    // the same value (since update didn't touch it).
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine e(cfg);

        struct PumpGame : IGame {
            PrePump* pumpPtr = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer& seed) override {
                EntityHandle h = eng.reserveEntityHandle();
                seed.spawn(h, Transform{});
                auto pump = std::make_unique<PrePump>(h);
                pumpPtr = pump.get();
                eng.registerSystem(std::move(pump));
            }
        };
        PumpGame g;
        CHECK(e.initialize(g));
        e.step();

        CHECK(g.pumpPtr->sawCorrectValueInUpdate);
        CHECK_EQ(g.pumpPtr->postStepSawValue, std::uint64_t{42});
        e.shutdown();
    }

    EXIT_WITH_RESULT();
}
