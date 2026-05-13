// §3.2 frameSnapshot + writeJsonLines: bundled snapshot of the per-tick
// instrumentation and a JSON-Lines serializer. Verifies:
//   - frameSnapshot() fields agree with the parallel
//     stats()/systemStats()/jobSystemStats() accessors
//   - writeJsonLines emits one '\n'-terminated record containing the
//     expected field names
//   - System names with embedded quotes/backslashes get escaped

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <sstream>
#include <string>

namespace {

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

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;
    Engine engine(cfg);

    struct Game : IGame {
        void onSetup(Engine&, World&, CommandBuffer&) override {}
    } game;
    CHECK(engine.initialize(game));

    engine.registerSystem(std::make_unique<TrivialSystem>("sysA"));
    engine.registerSystem(std::make_unique<TrivialSystem>("sys\"with\\quote"));

    engine.step();
    engine.step();

    const auto snap = engine.frameSnapshot();
    const auto raw  = engine.stats();
    const auto sys  = engine.systemStats();
    const auto jobs = engine.jobSystemStats();

    // Engine stats agree.
    CHECK_EQ(snap.engine.tick, raw.tick);
    CHECK_EQ(snap.engine.totalTicks, raw.totalTicks);
    CHECK_EQ(snap.engine.aliveEntities, raw.aliveEntities);

    // Systems span agrees in length and points at the same array.
    CHECK_EQ(snap.systems.size(), sys.size());
    CHECK(snap.systems.data() == sys.data());

    // Job stats agree.
    CHECK_EQ(snap.jobs.totalJobs, jobs.totalJobs);
    CHECK_EQ(snap.jobs.workerCount, jobs.workerCount);

    // Serialize and check structure.
    std::ostringstream os;
    writeJsonLines(os, snap);
    const std::string line = os.str();

    CHECK(!line.empty());
    CHECK(line.back() == '\n');                                      // newline-terminated
    CHECK(line.find("\"tick\":")   != std::string::npos);            // engine fields
    CHECK(line.find("\"step_s\":") != std::string::npos);
    CHECK(line.find("\"commit_s\":") != std::string::npos);
    CHECK(line.find("\"systems\":[") != std::string::npos);           // systems array
    CHECK(line.find("\"sysA\"")    != std::string::npos);
    CHECK(line.find("\"job_pool\":{") != std::string::npos);          // job pool block
    CHECK(line.find("\"workers\":") != std::string::npos);

    // The system named sys"with\quote should be escaped as
    // sys\"with\\quote inside JSON. Look for the escaped form.
    CHECK(line.find("sys\\\"with\\\\quote") != std::string::npos);

    // Exactly one '\n' (the terminator).
    std::size_t newlineCount = 0;
    for (char c : line) if (c == '\n') ++newlineCount;
    CHECK_EQ(newlineCount, std::size_t{1});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
