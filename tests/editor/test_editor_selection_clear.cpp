/// @file test_editor_selection_clear.cpp
/// @brief E6 — clear() drops the current selection.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/selection.hpp>

int main() {
    threadmaxx::editor::test::ScopedEngine env;

    threadmaxx::editor::SelectionState sel{env.engine().world()};
    sel.selectSystem(3);
    CHECK(sel.currentSelection().kind ==
          threadmaxx::editor::SelectionKind::System);

    sel.clear();
    CHECK(sel.currentSelection().kind ==
          threadmaxx::editor::SelectionKind::None);

    EXIT_WITH_RESULT();
}
