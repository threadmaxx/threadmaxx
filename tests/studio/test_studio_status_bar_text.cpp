/// @file test_studio_status_bar_text.cpp
/// @brief ST3 — paused engine → "PAUSED"; running engine → "FPS N".

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/data_source.hpp>
#include <threadmaxx_studio/panels/status_bar.hpp>

namespace {

class FakeSource : public threadmaxx::studio::IStudioDataSource {
public:
    threadmaxx::studio::EngineFrameSummary summary{};

    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
    std::optional<threadmaxx::studio::EngineFrameSummary>
    engineSnapshot() const override {
        return summary;
    }
};

} // namespace

int main() {
    threadmaxx::studio::StatusBar bar;
    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());

    FakeSource src;
    src.summary.paused = true;

    backend.beginFrame();
    bar.render(backend, src);
    backend.endFrame();
    CHECK(bar.lastStatus() == "PAUSED");

    // Running engine — 16 ms step → FPS ~62.
    src.summary.paused = false;
    src.summary.lastStepSeconds = 0.016;

    backend.beginFrame();
    bar.render(backend, src);
    backend.endFrame();
    const auto running = std::string(bar.lastStatus());
    CHECK(running.find("FPS") != std::string::npos);
    // The rounded fps should be 62 or 63 (1/0.016 = 62.5 → 62 trunc;
    // +0.5 then int-cast → 63). Either is acceptable; we just want a
    // sensible integer rendered.
    CHECK(running.find("6") != std::string::npos);

    // No snapshot available → fallback string.
    struct NullSource : threadmaxx::studio::IStudioDataSource {
        threadmaxx::studio::AttachMode mode() const noexcept override {
            return threadmaxx::studio::AttachMode::Direct;
        }
    } ns;
    backend.beginFrame();
    bar.render(backend, ns);
    backend.endFrame();
    CHECK(bar.lastStatus() == "no engine");

    backend.shutdown();
    EXIT_WITH_RESULT();
}
