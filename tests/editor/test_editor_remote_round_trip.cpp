/// @file test_editor_remote_round_trip.cpp
/// @brief E14 — recording into a RemoteBackend and decoding into a
/// HeadlessBackend reproduces every captured op in order.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/backends/remote.hpp>

int main() {
    using namespace threadmaxx::editor;

    RemoteBackend recorder;
    CHECK(recorder.initialize());
    CHECK(recorder.initialized());

    recorder.beginFrame();
    recorder.drawText("hello", 4.0f, 8.5f);
    recorder.drawRect(0.0f, 0.0f, 100.0f, 50.0f);
    recorder.drawText("", 1.0f, 2.0f); // empty string edge case
    recorder.endFrame();

    HeadlessBackend sink;
    CHECK(sink.initialize());

    const auto bytes = recorder.bytes();
    CHECK(bytes.size() > 0);
    const auto consumed = decodeRemoteStream(bytes, sink);
    CHECK_EQ(consumed, bytes.size());

    const auto& frame = sink.capturedFrame();
    CHECK_EQ(frame.ops.size(), 5u);
    CHECK(frame.ops[0].op == CapturedOp::Op::BeginFrame);

    CHECK(frame.ops[1].op == CapturedOp::Op::DrawText);
    CHECK(frame.ops[1].text == "hello");
    CHECK(frame.ops[1].x == 4.0f);
    CHECK(frame.ops[1].y == 8.5f);

    CHECK(frame.ops[2].op == CapturedOp::Op::DrawRect);
    CHECK(frame.ops[2].x == 0.0f);
    CHECK(frame.ops[2].w == 100.0f);
    CHECK(frame.ops[2].h == 50.0f);

    CHECK(frame.ops[3].op == CapturedOp::Op::DrawText);
    CHECK(frame.ops[3].text == "");
    CHECK(frame.ops[3].x == 1.0f);

    CHECK(frame.ops[4].op == CapturedOp::Op::EndFrame);

    sink.shutdown();
    recorder.shutdown();
    CHECK(!recorder.initialized());

    EXIT_WITH_RESULT();
}
