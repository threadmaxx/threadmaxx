#pragma once

/// @file session.hpp
/// @brief EditorSession — the editor's bound view of a live Engine.

#include "types.hpp"

#include <threadmaxx/Engine.hpp>

namespace threadmaxx::editor {

class IEditorBackend;

/// @brief Editor session over a live Engine.
///
/// The session is the editor's connection to the running simulation.
/// It is non-owning: the engine outlives the session by convention.
/// Multiple sessions over the same engine are allowed but not
/// recommended; later panels (Inspector, Selection) cache per-session
/// state that does not coordinate.
///
/// @thread_safety The session is sim-thread-local by convention.
class EditorSession {
public:
    /// @brief Attach to a live engine. The session captures the engine
    /// reference and a session id; nothing else happens until the host
    /// installs a backend via @ref setBackend.
    explicit EditorSession(threadmaxx::Engine& engine) noexcept;

    ~EditorSession();

    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;
    EditorSession(EditorSession&&) = delete;
    EditorSession& operator=(EditorSession&&) = delete;

    /// @brief Stable per-session id, monotonically increasing per
    /// process. The first session returns id `{1}`.
    SessionId id() const noexcept { return id_; }

    threadmaxx::Engine&       engine()       noexcept { return *engine_; }
    const threadmaxx::Engine& engine() const noexcept { return *engine_; }

    /// @brief Install (or detach) a UI backend. The session does NOT
    /// take ownership; the backend must outlive the session. The first
    /// call invokes `backend->initialize()`; passing `nullptr` (or a
    /// new backend) invokes `shutdown()` on the previously installed
    /// one. Returns true if initialize() succeeded (or `backend` is
    /// nullptr).
    bool setBackend(IEditorBackend* backend);

    IEditorBackend* backend() const noexcept { return backend_; }

private:
    threadmaxx::Engine* engine_{nullptr};
    IEditorBackend* backend_{nullptr};
    SessionId id_{};
};

} // namespace threadmaxx::editor
