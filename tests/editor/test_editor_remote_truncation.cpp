/// @file test_editor_remote_truncation.cpp
/// @brief E14 — a truncated buffer returns 0 (no sink call is made
/// after the failing op); a buffer with an unknown tag also returns 0.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/backends/remote.hpp>

#include <vector>

int main() {
    using namespace threadmaxx::editor;

    RemoteBackend recorder;
    recorder.beginFrame();
    recorder.drawText("hi", 1.0f, 2.0f);
    recorder.endFrame();

    const auto good = recorder.bytes();
    CHECK(good.size() > 0);

    // Trim EndFrame + both chars of "hi" — DrawText's `len` is 2
    // but the buffer has 0 bytes remaining.
    std::vector<std::byte> truncated(good.begin(), good.end() - 3);
    {
        HeadlessBackend sink;
        sink.initialize();
        const auto consumed = decodeRemoteStream(
            std::span<const std::byte>(truncated), sink);
        CHECK_EQ(consumed, 0u);
        // BeginFrame succeeded before truncation surfaced; DrawText
        // returns 0 without forwarding to the sink.
        CHECK_EQ(sink.capturedFrame().ops.size(), 1u);
        sink.shutdown();
    }

    // Unknown tag (0xFF) in the middle of an otherwise-valid buffer.
    {
        std::vector<std::byte> bad{std::byte{0xFF}};
        HeadlessBackend sink;
        sink.initialize();
        const auto consumed = decodeRemoteStream(
            std::span<const std::byte>(bad), sink);
        CHECK_EQ(consumed, 0u);
        CHECK_EQ(sink.capturedFrame().ops.size(), 0u);
        sink.shutdown();
    }

    // Empty input is fine — zero ops, zero bytes consumed.
    {
        HeadlessBackend sink;
        sink.initialize();
        const auto consumed = decodeRemoteStream({}, sink);
        CHECK_EQ(consumed, 0u);
        CHECK_EQ(sink.capturedFrame().ops.size(), 0u);
        sink.shutdown();
    }

    EXIT_WITH_RESULT();
}
