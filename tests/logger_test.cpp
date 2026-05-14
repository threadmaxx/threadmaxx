// §3.1 ILogger: a capture-logger sees lifecycle messages (initialize,
// registerSystem, shutdown) and the default sink is in place when no
// custom logger is installed.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <mutex>
#include <string>
#include <vector>

namespace {

struct CapturedRecord {
    threadmaxx::LogLevel level;
    std::string          message;
};

class CapturingLogger : public threadmaxx::ILogger {
public:
    void log(threadmaxx::LogLevel level, std::string_view msg) override {
        std::lock_guard<std::mutex> lk(mtx_);
        records_.push_back({level, std::string(msg)});
    }
    std::vector<CapturedRecord> records() {
        std::lock_guard<std::mutex> lk(mtx_);
        return records_;
    }
private:
    std::mutex                  mtx_;
    std::vector<CapturedRecord> records_;
};

class TrivialSystem : public threadmaxx::ISystem {
public:
    explicit TrivialSystem(const char* n) : name_(n) {}
    const char* name() const noexcept override { return name_; }
    void update(threadmaxx::SystemContext&) override {}
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
private:
    const char* name_;
};

bool containsSubstr(const std::vector<CapturedRecord>& recs, std::string_view needle) {
    for (const auto& r : recs) {
        if (r.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

int main() {
    using namespace threadmaxx;

    // Test 1: a CapturingLogger sees init / registerSystem / shutdown.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        CapturingLogger logger;
        engine.setLogger(&logger);

        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } game;
        CHECK(engine.initialize(game));
        engine.registerSystem(std::make_unique<TrivialSystem>("alpha"));
        engine.registerSystem(std::make_unique<TrivialSystem>("beta"));
        engine.step();
        engine.shutdown();

        const auto recs = logger.records();
        // Expect at least: initialize, two registers, shutdown.
        CHECK(recs.size() >= 4);
        CHECK(containsSubstr(recs, "engine initialize"));
        CHECK(containsSubstr(recs, "alpha"));
        CHECK(containsSubstr(recs, "beta"));
        CHECK(containsSubstr(recs, "engine shutdown"));
    }

    // Test 2: default logger is in place when no setLogger call is made.
    // We can't easily assert what it writes (goes to std::cerr) but the
    // accessor must return a non-trivial reference.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        ILogger& l = engine.logger();
        // The reference is valid; this is the "default sink installed"
        // contract. Emitting at trace shouldn't crash.
        l.log(LogLevel::Trace, "test trace");
        l.log(LogLevel::Warn,  "test warn (default sink may print)");
        (void)l;
    }

    // Test 3: setting nullptr restores the default sink.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        CapturingLogger logger;
        engine.setLogger(&logger);

        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } game;
        engine.initialize(game);
        CHECK(!logger.records().empty());

        engine.setLogger(nullptr);
        // The default sink is now reachable via logger(); accessor must
        // not crash and must not be the same address as our capturer.
        ILogger& def = engine.logger();
        CHECK(&def != static_cast<ILogger*>(&logger));

        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
