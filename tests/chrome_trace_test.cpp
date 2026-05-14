// §3.1 ChromeTraceWriter: emits a syntactically valid JSON array of
// `{ph:"X",...}` records, opens with `[`, closes with `]`, and
// includes one event per system per snapshot plus a "step" framer.

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

std::size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    std::size_t n = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);

    struct G : IGame {
        void onSetup(Engine&, World&, CommandBuffer&) override {}
    } g;
    engine.initialize(g);
    engine.registerSystem(std::make_unique<TrivialSystem>("alpha"));
    engine.registerSystem(std::make_unique<TrivialSystem>("beta"));

    std::ostringstream os;
    {
        ChromeTraceWriter w(os);
        for (int i = 0; i < 3; ++i) {
            engine.step();
            w.emit(engine.frameSnapshot());
        }
    } // dtor writes closing ']'

    const std::string out = os.str();

    // Brackets are present and well-placed.
    CHECK(!out.empty());
    CHECK(out.front() == '[');
    CHECK(out.back() == '\n');
    CHECK(out.find(']') != std::string::npos);

    // 3 snapshots * (1 step record + 2 system records) = 9 `"ph":"X"` events.
    CHECK_EQ(countOccurrences(out, "\"ph\":\"X\""), std::size_t{9});

    // 3 "step" records.
    CHECK_EQ(countOccurrences(out, "\"name\":\"step\""), std::size_t{3});

    // System names appear N times each (1 per snapshot).
    CHECK_EQ(countOccurrences(out, "\"name\":\"alpha\""), std::size_t{3});
    CHECK_EQ(countOccurrences(out, "\"name\":\"beta\""),  std::size_t{3});

    // pid is always 1; tid is non-zero for systems.
    CHECK(out.find("\"pid\":1") != std::string::npos);

    // ts and dur fields appear on every event.
    CHECK_EQ(countOccurrences(out, "\"ts\":"),  std::size_t{9});
    CHECK_EQ(countOccurrences(out, "\"dur\":"), std::size_t{9});

    // Verify no record has a leading or trailing dangling comma — i.e.
    // there is no ",]" or "[," in the output.
    CHECK(out.find(",]") == std::string::npos);
    CHECK(out.find("[,") == std::string::npos);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
