/// @file test_studio_audio_bus_graph.cpp
/// @brief ST16 — AudioPanel renders one header + one row per bus
/// (master + N user buses); `busRowCount()` matches `listBuses().size()`.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/audio.hpp>

#include <threadmaxx_audio/threadmaxx_audio.hpp>

#include <memory>

namespace {

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    studio::AudioPanel panel;

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.busRowCount(), 0u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    // Wire a mixer with master + 2 user buses.
    auto deviceOwner = std::make_unique<audio::LoopbackDevice>();
    audio::AudioMixer mixer(std::move(deviceOwner));
    CHECK(mixer.initialize(
        audio::AudioFormat{48000, 2, audio::ChannelLayout::Stereo}, 256));
    panel.setMixer(&mixer);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.busRowCount(), 1u); // master-only
    CHECK_EQ(countTextOps(backend.capturedFrame()), 2u); // header + master

    audio::BusDesc d1{}; d1.gainDb = -6.0f;
    [[maybe_unused]] auto b1 = mixer.createBus(d1);
    audio::BusDesc d2{}; d2.muted = true;
    [[maybe_unused]] auto b2 = mixer.createBus(d2);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.busRowCount(), 3u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 4u);

    backend.shutdown();
    mixer.shutdown();
    EXIT_WITH_RESULT();
}
