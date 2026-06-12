/// @file HeadlessBackend.cpp
/// @brief Capture-only backend used by the editor test suite.

#include "threadmaxx_editor/backends/headless.hpp"

namespace threadmaxx::editor {

bool HeadlessBackend::initialize() {
    initialized_ = true;
    return true;
}

void HeadlessBackend::shutdown() {
    initialized_ = false;
    inFrame_ = false;
    frame_.clear();
}

void HeadlessBackend::beginFrame() {
    frame_.clear();
    inFrame_ = true;
    frame_.ops.push_back({CapturedOp::Op::BeginFrame, {}, 0, 0, 0, 0});
}

void HeadlessBackend::endFrame() {
    frame_.ops.push_back({CapturedOp::Op::EndFrame, {}, 0, 0, 0, 0});
    inFrame_ = false;
}

void HeadlessBackend::drawText(std::string_view text, float x, float y) {
    frame_.ops.push_back(
        {CapturedOp::Op::DrawText, std::string(text), x, y, 0, 0});
}

void HeadlessBackend::drawRect(float x, float y, float w, float h) {
    frame_.ops.push_back({CapturedOp::Op::DrawRect, {}, x, y, w, h});
}

} // namespace threadmaxx::editor
