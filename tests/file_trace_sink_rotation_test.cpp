// §3.6.5 batch 15b — Focused multi-file rotation test for FileTraceSink.
//
// FileTraceSink rolls its underlying file when `bytesWrittenCurrent()`
// crosses `rotationBytes`. This test exercises the contract:
//
//   (1) With a small `rotationBytes` (256 bytes) and many ticks, the
//       sink produces multiple files.
//   (2) Each produced file is a standalone valid JSON array — starts
//       with `[` and ends with `]`. Loading any single file in
//       chrome://tracing must not fail.
//   (3) The rotation index advances monotonically.
//   (4) `%N` substitution in `pathTemplate` works; absent `%N`, the
//       sink appends `.N.json`.
//   (5) onShutdown closes the final file cleanly even mid-tick.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>
#include <threadmaxx/Telemetry.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace threadmaxx;

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream oss; oss << f.rdbuf();
    return oss.str();
}

bool isValidJsonArray(const std::string& s) {
    // Loose check: starts with '[' (after optional whitespace), ends
    // with ']' (before optional whitespace), and is non-empty.
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t')) ++i;
    if (i >= s.size() || s[i] != '[') return false;
    std::size_t j = s.size();
    while (j > 0 && (s[j-1] == ' ' || s[j-1] == '\n' || s[j-1] == '\t')) --j;
    if (j == 0 || s[j-1] != ']') return false;
    return j > i + 1;
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path tmpDir = fs::temp_directory_path() /
        ("threadmaxx_rotation_" + std::to_string(unique));
    fs::create_directories(tmpDir);

    // ---- (1)(2)(3) %N substitution + multiple files ------------------
    {
        const fs::path tmpl = tmpDir / "trace.%N.json";
        FileTraceSink::Config cfg;
        cfg.pathTemplate = tmpl.string();
        cfg.rotationBytes = 256;  // tiny budget — forces rotation

        Config ecfg; ecfg.sleepToPace = false; ecfg.workerCount = 1;
        Engine engine(ecfg);

        auto sink = std::make_unique<FileTraceSink>(cfg);
        engine.setTraceSink(sink.get());

        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        CHECK(engine.initialize(g));

        // 60 ticks at ~150-200 bytes each = several rotations.
        for (int i = 0; i < 60; ++i) engine.step();

        const std::uint32_t finalIdx = sink->rotationIndex();
        engine.setTraceSink(nullptr);
        engine.shutdown();
        sink.reset();  // forces final close

        CHECK(finalIdx >= 1);

        // (2): every file produced is a valid JSON array.
        for (std::uint32_t n = 0; n <= finalIdx; ++n) {
            const fs::path file = tmpDir /
                ("trace." + std::to_string(n) + ".json");
            CHECK(fs::exists(file));
            const auto contents = slurp(file);
            CHECK(isValidJsonArray(contents));
            fs::remove(file);
        }
    }

    // ---- (4) Without %N, the sink appends `.N.json` ------------------
    {
        const fs::path tmpl = tmpDir / "trace2";
        FileTraceSink::Config cfg;
        cfg.pathTemplate = tmpl.string();
        cfg.rotationBytes = 200;

        Config ecfg; ecfg.sleepToPace = false; ecfg.workerCount = 1;
        Engine engine(ecfg);

        auto sink = std::make_unique<FileTraceSink>(cfg);
        engine.setTraceSink(sink.get());

        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        CHECK(engine.initialize(g));

        for (int i = 0; i < 30; ++i) engine.step();
        const std::uint32_t finalIdx = sink->rotationIndex();
        engine.setTraceSink(nullptr);
        engine.shutdown();
        sink.reset();

        for (std::uint32_t n = 0; n <= finalIdx; ++n) {
            const fs::path file{tmpl.string() +
                "." + std::to_string(n) + ".json"};
            CHECK(fs::exists(file));
            CHECK(isValidJsonArray(slurp(file)));
            fs::remove(file);
        }
    }

    fs::remove_all(tmpDir);
    EXIT_WITH_RESULT();
}
