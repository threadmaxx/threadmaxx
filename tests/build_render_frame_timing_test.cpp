// §3.2 batch 8: ordering — buildRenderFrame hook runs after every
// postStep has committed, on the sim thread. The hook sees the world
// state that postStep just produced.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Transcript {
    std::vector<std::string> events;
    std::mutex               mtx;  // update() runs concurrently across systems
    void record(std::string s) {
        std::lock_guard<std::mutex> lk(mtx);
        events.push_back(std::move(s));
    }
};

class TracingSystem : public threadmaxx::ISystem {
public:
    TracingSystem(const char* name, Transcript& t) : name_(name), t_(t) {}
    const char* name() const noexcept override { return name_; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void preStep(threadmaxx::SystemContext&) override {
        t_.record(std::string("pre:") + name_);
    }
    void update(threadmaxx::SystemContext&) override {
        t_.record(std::string("update:") + name_);
    }
    void postStep(threadmaxx::SystemContext&) override {
        t_.record(std::string("post:") + name_);
    }
    void buildRenderFrame(threadmaxx::RenderFrameBuilder&) override {
        t_.record(std::string("build:") + name_);
    }

private:
    const char* name_;
    Transcript& t_;
};

class Game : public threadmaxx::IGame {
public:
    Game(Transcript& t) : t_(t) {}
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {
        engine.registerSystem(std::make_unique<TracingSystem>("A", t_));
        engine.registerSystem(std::make_unique<TracingSystem>("B", t_));
    }
    Transcript& t_;
};

} // namespace

int main() {
    Transcript transcript;
    threadmaxx::Engine engine(threadmaxx::Config{});
    Game game(transcript);
    CHECK(engine.initialize(game));
    engine.step();
    engine.shutdown();

    // Expected order for one tick:
    //   pre:A pre:B   (registration order)
    //   update:A update:B  (wave order; here both have empty read/write so they share a wave)
    //   post:A post:B  (registration order)
    //   build:A build:B  (registration order, AFTER post)
    // Verify the relative ordering: every "build:*" follows every "post:*".
    std::size_t lastPost = 0;
    std::size_t firstBuild = transcript.events.size();
    for (std::size_t i = 0; i < transcript.events.size(); ++i) {
        if (transcript.events[i].rfind("post:", 0) == 0) lastPost = i;
        if (transcript.events[i].rfind("build:", 0) == 0 && i < firstBuild) firstBuild = i;
    }
    CHECK(firstBuild > lastPost);

    // Both systems' build hooks fire.
    int buildCount = 0;
    for (const auto& e : transcript.events) {
        if (e.rfind("build:", 0) == 0) ++buildCount;
    }
    CHECK_EQ(buildCount, 2);

    EXIT_WITH_RESULT();
}
