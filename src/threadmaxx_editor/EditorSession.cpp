/// @file EditorSession.cpp
/// @brief EditorSession lifetime + backend orchestration.

#include "threadmaxx_editor/session.hpp"

#include "threadmaxx_editor/backend.hpp"

#include <atomic>

namespace threadmaxx::editor {

namespace {

std::atomic<std::uint64_t> g_nextSessionId{1};

} // namespace

EditorSession::EditorSession(threadmaxx::Engine& engine) noexcept
    : engine_(&engine),
      backend_(nullptr),
      id_{g_nextSessionId.fetch_add(1, std::memory_order_relaxed)} {}

EditorSession::~EditorSession() {
    if (backend_) {
        backend_->shutdown();
        backend_ = nullptr;
    }
}

bool EditorSession::setBackend(IEditorBackend* backend) {
    if (backend_ == backend) return true;
    if (backend_) {
        backend_->shutdown();
        backend_ = nullptr;
    }
    if (!backend) return true;
    if (!backend->initialize()) return false;
    backend_ = backend;
    return true;
}

} // namespace threadmaxx::editor
