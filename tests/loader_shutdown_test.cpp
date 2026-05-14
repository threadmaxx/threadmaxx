// §3.2 batch 7: IResourceLoader::onShutdown is called by Engine::shutdown
// in reverse-registration order, before each loader is destroyed. The
// engine guarantees update() is not called after onShutdown() returns.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <vector>

namespace {

// Two-phase log: 'S' = onShutdown, 'D' = destructor. Tag identifies the
// loader. We expect: onShutdown order is [last, ..., first]; destructor
// order is the same; AND for any given loader its 'S' precedes its 'D'.
struct LogEntry { char phase; int tag; };

class TracingLoader : public threadmaxx::IResourceLoader {
public:
    std::vector<LogEntry>* log = nullptr;
    int tag = 0;
    int updates = 0;
    bool shutdownCalled = false;

    ~TracingLoader() override {
        if (log) log->push_back({'D', tag});
    }

    void update(threadmaxx::Engine&) override {
        ++updates;
    }

    void onShutdown(threadmaxx::Engine&) override {
        if (log) log->push_back({'S', tag});
        shutdownCalled = true;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    std::vector<LogEntry> log;

    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
        CHECK(engine.initialize(game));

        for (int t = 1; t <= 3; ++t) {
            auto loader = std::make_unique<TracingLoader>();
            loader->log = &log;
            loader->tag = t;
            engine.addResourceLoader(std::move(loader));
        }
        CHECK_EQ(engine.resourceLoaderCount(), std::size_t{3});

        engine.step();
        engine.step();

        engine.shutdown();
    }

    // Expected sequence: S3, S2, S1, D3, D2, D1.
    CHECK_EQ(log.size(), std::size_t{6});
    CHECK_EQ(log[0].phase, 'S'); CHECK_EQ(log[0].tag, 3);
    CHECK_EQ(log[1].phase, 'S'); CHECK_EQ(log[1].tag, 2);
    CHECK_EQ(log[2].phase, 'S'); CHECK_EQ(log[2].tag, 1);
    CHECK_EQ(log[3].phase, 'D'); CHECK_EQ(log[3].tag, 3);
    CHECK_EQ(log[4].phase, 'D'); CHECK_EQ(log[4].tag, 2);
    CHECK_EQ(log[5].phase, 'D'); CHECK_EQ(log[5].tag, 1);

    EXIT_WITH_RESULT();
}
