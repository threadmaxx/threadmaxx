/// @file test_editor_selection_stale_entity.cpp
/// @brief E6 — entity handle whose generation no longer matches a
/// live slot is auto-cleared on next currentSelection() access.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/selection.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>

int main() {
    threadmaxx::editor::test::ScopedEngine env;

    threadmaxx::editor::SelectionState sel{env.engine().world()};

    // A handle that was never alive (bogus generation) is auto-cleared.
    threadmaxx::EntityHandle ghost{42, 999};
    sel.select(ghost);
    const auto s = sel.currentSelection();
    CHECK(s.kind == threadmaxx::editor::SelectionKind::None);

    EXIT_WITH_RESULT();
}
