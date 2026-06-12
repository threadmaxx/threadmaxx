#pragma once

/// @file studio.hpp
/// @brief `StudioApp` + `PanelHost` — the top-level shell of the
/// studio. Hosts panels, persists layout via `editor::LayoutManager`,
/// and tracks running/stopped lifecycle. Concrete data sources and
/// concrete panels land in later ST batches.

#include "panel.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace threadmaxx::editor {
class EditorSession;
class LayoutManager;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

/// @brief Container for registered panels.
///
/// Non-owning: the host outlives every panel pointer it accepts.
/// Panel ids are unique within a single host; double-registration
/// returns `false`. Visibility shadow state is persisted into and
/// restored from `editor::LayoutManager::state().panels` — the
/// studio never defines a parallel layout system.
///
/// @thread_safety Single-threaded; UI-thread-local by convention.
class PanelHost {
public:
    PanelHost() = default;
    PanelHost(const PanelHost&) = delete;
    PanelHost& operator=(const PanelHost&) = delete;

    /// @brief Register a panel. Returns `false` if `panel` is null,
    /// an existing panel already owns the id, or the per-host cap
    /// (`kMaxPanels`) is reached. Panels default to visible.
    bool registerPanel(IStudioPanel* panel);

    /// @brief Unregister by id. Returns `false` if no match.
    bool unregisterPanel(std::string_view id);

    /// @brief Look up a panel by id. Returns `nullptr` if absent.
    IStudioPanel* findPanel(std::string_view id) const noexcept;

    /// @brief Number of registered panels.
    std::size_t panelCount() const noexcept { return slots_.size(); }

    /// @brief Set per-panel visibility. No-op if id is unknown.
    void setVisible(std::string_view id, bool visible);

    /// @brief Returns `false` if id is unknown.
    bool isVisible(std::string_view id) const noexcept;

    /// @brief Write current visibility state into the layout manager.
    /// Existing entries with no matching panel are left untouched.
    void saveTo(editor::LayoutManager& mgr) const;

    /// @brief Pull visibility from the layout manager. Panels not
    /// mentioned in the manager keep their current visibility.
    void restoreFrom(const editor::LayoutManager& mgr);

private:
    struct Slot {
        IStudioPanel* panel{nullptr};
        bool visible{true};
    };
    std::vector<Slot> slots_;
};

/// @brief Top-level studio application.
///
/// Lifecycle: construct over an `editor::EditorSession` (the editor
/// owns the engine attachment); call `start()` to enter the running
/// state; `stop()` to leave it. Both are idempotent. `~StudioApp`
/// calls `stop()` so the destruction order can't strand panels in a
/// half-attached state. ST2 does not yet drive per-frame rendering;
/// future ST batches add a `tick()` / `render()` pump.
///
/// @thread_safety Single-threaded; UI-thread-local by convention.
class StudioApp {
public:
    explicit StudioApp(editor::EditorSession& session) noexcept;
    ~StudioApp();

    StudioApp(const StudioApp&) = delete;
    StudioApp& operator=(const StudioApp&) = delete;

    /// @brief Enter the running state. Idempotent; returns `true` if
    /// the app is running at the moment the call returns.
    bool start();

    /// @brief Leave the running state. Idempotent.
    void stop();

    bool running() const noexcept { return running_; }

    PanelHost&       panelHost()       noexcept { return host_; }
    const PanelHost& panelHost() const noexcept { return host_; }

    editor::EditorSession&       session()       noexcept { return *session_; }
    const editor::EditorSession& session() const noexcept { return *session_; }

private:
    editor::EditorSession* session_{nullptr};
    PanelHost host_{};
    bool running_{false};
};

} // namespace threadmaxx::studio
