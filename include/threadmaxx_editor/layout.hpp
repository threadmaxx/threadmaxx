#pragma once

/// @file layout.hpp
/// @brief Editor layout state — which panels are visible, dock
/// configuration, selected tab — plus stream-based save/load.

#include <iosfwd>
#include <string>
#include <unordered_map>

namespace threadmaxx::editor {

/// @brief Editor layout state. `panels` is a panel-name → visibility
/// map; `dockJson` is a backend-specific blob (e.g. Dear ImGui's dock
/// state) that the editor neither parses nor validates.
struct LayoutState {
    std::unordered_map<std::string, bool> panels;
    std::string dockJson;
    std::string selectedPanel;
};

/// @brief Persists and restores `LayoutState` to/from a stream. Wire
/// format is a simple `key=value\n` text stream — small, diff-able in
/// version control, and easy to hand-edit during development.
class LayoutManager {
public:
    explicit LayoutManager(LayoutState state = {}) noexcept
        : state_(std::move(state)) {}

    LayoutState& state() noexcept { return state_; }
    const LayoutState& state() const noexcept { return state_; }

    /// @brief Write the current state.
    void save(std::ostream& out) const;

    /// @brief Read state from `in`, replacing the current state.
    /// Returns false on a malformed stream; on failure the current
    /// state is left unchanged.
    bool load(std::istream& in);

private:
    LayoutState state_;
};

} // namespace threadmaxx::editor
