/// @file test_studio_tuning_patch_apply.cpp
/// @brief ST13 — TuningPanel reports the engine's tuning mode /
/// policy / trace state; applyPatch records a TuningPatch onto the
/// engine's installed TuningTrace.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/tuning.hpp>

namespace {

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    threadmaxx::studio::test::ScopedSession env{};
    threadmaxx::studio::TuningPanel panel{env.engine()};
    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    NullSource src;

    // No trace installed → applyPatch rejects.
    threadmaxx::TuningPatch p{};
    p.grainOverrides.push_back({"Adder", 64});
    CHECK(!panel.applyPatch(1, p));
    CHECK_EQ(panel.traceSize(), 0u);

    // Install a trace via the engine API; applyPatch records.
    threadmaxx::TuningTrace trace;
    env.engine().setTuningTrace(&trace);
    CHECK(panel.applyPatch(7, p));
    CHECK_EQ(panel.traceSize(), 1u);

    // Verify the recorded payload.
    threadmaxx::TuningPatch out{};
    CHECK(trace.tryGet(7, out));
    CHECK_EQ(out.grainOverrides.size(), 1u);
    CHECK(out.grainOverrides[0].systemName == "Adder");
    CHECK_EQ(out.grainOverrides[0].preferredGrain, 64u);

    // Render captures a single status line summarizing the state.
    backend.beginFrame();
    panel.render(backend, src);
    backend.endFrame();
    bool foundStatus = false;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op != threadmaxx::editor::CapturedOp::Op::DrawText) continue;
        if (op.text.find("trace=installed") != std::string::npos &&
            op.text.find("entries=1") != std::string::npos) {
            foundStatus = true;
        }
    }
    CHECK(foundStatus);

    env.engine().setTuningTrace(nullptr);
    backend.shutdown();
    EXIT_WITH_RESULT();
}
