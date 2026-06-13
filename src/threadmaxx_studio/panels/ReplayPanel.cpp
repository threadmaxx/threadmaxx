/// @file panels/ReplayPanel.cpp
/// @brief ST23 — `ReplayPanel` implementation.

#include <threadmaxx_studio/panels/replay.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/replay.hpp>

#include <cstdio>

namespace threadmaxx::studio {

ReplayPanel::~ReplayPanel() {
    teardownSession();
}

void ReplayPanel::teardownSession() {
    delete session_;
    session_ = nullptr;
}

void ReplayPanel::setStream(const editor::CaptureStream* stream) {
    teardownSession();
    stream_ = stream;
    if (stream_ != nullptr) {
        session_ = new editor::ReplaySession(*stream_);
    }
}

void ReplayPanel::seek(std::size_t index) {
    if (session_ != nullptr) session_->seek(index);
}

void ReplayPanel::step(std::int64_t delta) {
    if (session_ != nullptr) session_->step(delta);
}

std::size_t ReplayPanel::cursor() const noexcept {
    return session_ != nullptr ? session_->cursor() : 0u;
}

std::uint64_t ReplayPanel::currentTick() const noexcept {
    return session_ != nullptr ? session_->currentTick() : 0u;
}

void ReplayPanel::render(editor::IEditorBackend& backend,
                         IStudioDataSource&) {
    if (session_ == nullptr) {
        backend.drawText("Replay: <no stream bound>", 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Replay  frame=%zu/%zu  tick=%llu",
                  session_->cursor(),
                  session_->frameCount(),
                  static_cast<unsigned long long>(session_->currentTick()));
    backend.drawText(buf, 0.0f, 0.0f);

    const auto entities = session_->listEntities();
    std::snprintf(buf, sizeof(buf), "entities=%zu", entities.size());
    backend.drawText(buf, 0.0f, 16.0f);

    float y = 32.0f;
    std::size_t shown = 0;
    for (const auto& e : entities) {
        if (shown >= maxRows_) break;
        backend.drawText(e.label, 0.0f, y);
        y += 14.0f;
        ++shown;
    }
    lastRows_ = 2 + shown;
}

} // namespace threadmaxx::studio
