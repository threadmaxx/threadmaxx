#pragma once

/// @file panel.hpp
/// @brief `IStudioPanel` — the polymorphic panel interface every studio
/// panel implements.
///
/// Panels render through an `editor::IEditorBackend` (so the studio
/// stays UI-toolkit-agnostic) and read engine / sibling state through
/// an `IStudioDataSource` (so the same panel works in-process and
/// over the editor v1.2 remote wire).
///
/// This header is intentionally engine-free: it does not pull any
/// `threadmaxx/` core header, so a host can ship an out-of-process
/// studio binary that links only `threadmaxx::editor` +
/// `threadmaxx::studio` + a backend (e.g. ImGui).

#include "data_source.hpp"

#include <string_view>

namespace threadmaxx::editor {
class IEditorBackend;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

/// @brief Polymorphic panel interface.
///
/// Lifecycle: the host registers an `IStudioPanel` with the
/// `PanelHost` (ST2); the host then calls `render()` once per frame
/// for each visible panel. Panels are reentrant only across distinct
/// instances; the framework never calls `render()` on the same
/// instance from two threads.
///
/// @thread_safety Single-threaded; the studio renders panels on the
/// host's UI thread.
class IStudioPanel {
public:
    virtual ~IStudioPanel() = default;

    /// @brief Stable identifier for layout persistence and lookup.
    /// Must be unique within a `PanelHost`. Convention: lowercase
    /// dotted segments, e.g. `"engine.inspector"`.
    virtual std::string_view id() const noexcept = 0;

    /// @brief Human-readable title shown in window chrome.
    virtual std::string_view title() const noexcept = 0;

    /// @brief Draw one frame.
    /// @param backend Renderer-neutral draw surface owned by the host.
    /// @param source Read / mutate world state through this; panels
    /// MUST NOT reach around the source.
    virtual void render(editor::IEditorBackend& backend,
                        IStudioDataSource& source) = 0;

    /// @brief Notified when the host swaps attach mode (Shape A ↔
    /// Shape B). Default no-op; panels that cache between frames may
    /// invalidate their cache here.
    virtual void onAttachChanged(AttachMode /*newMode*/) {}
};

} // namespace threadmaxx::studio
