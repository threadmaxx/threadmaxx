/// @file test_editor_headless_backend_capture.cpp
/// @brief E1 — HeadlessBackend records every draw call into its
/// captured frame in submission order.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>

int main() {
    using namespace threadmaxx::editor;

    HeadlessBackend back;
    CHECK(back.initialize());
    CHECK(back.initialized());

    back.beginFrame();
    CHECK(back.inFrame());
    back.drawText("hello", 10.0f, 20.0f);
    back.drawRect(0.0f, 0.0f, 100.0f, 100.0f);
    back.endFrame();
    CHECK(!back.inFrame());

    const auto& frame = back.capturedFrame();
    CHECK_EQ(frame.size(), 4u);
    CHECK(frame.ops[0].op == CapturedOp::Op::BeginFrame);
    CHECK(frame.ops[1].op == CapturedOp::Op::DrawText);
    CHECK(frame.ops[1].text == "hello");
    CHECK_EQ(frame.ops[1].x, 10.0f);
    CHECK_EQ(frame.ops[1].y, 20.0f);
    CHECK(frame.ops[2].op == CapturedOp::Op::DrawRect);
    CHECK_EQ(frame.ops[2].w, 100.0f);
    CHECK(frame.ops[3].op == CapturedOp::Op::EndFrame);

    back.shutdown();
    CHECK(!back.initialized());
    EXIT_WITH_RESULT();
}
