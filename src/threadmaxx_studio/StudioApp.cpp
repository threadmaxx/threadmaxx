/// @file StudioApp.cpp
/// @brief `StudioApp` impl. ST2 lifecycle is intentionally thin —
/// future ST batches add per-frame pump, menu / status / console
/// hookup (ST3), and the data source plumbing (ST4).

#include <threadmaxx_studio/studio.hpp>

#include <threadmaxx_editor/session.hpp>

namespace threadmaxx::studio {

StudioApp::StudioApp(editor::EditorSession& session) noexcept
    : session_(&session) {}

StudioApp::~StudioApp() {
    stop();
}

bool StudioApp::start() {
    running_ = true;
    return running_;
}

void StudioApp::stop() {
    running_ = false;
}

} // namespace threadmaxx::studio
