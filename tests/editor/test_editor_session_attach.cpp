/// @file test_editor_session_attach.cpp
/// @brief E1 — construct an EditorSession over an Engine; engine()
/// returns the same instance; session ids are unique and non-zero.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/session.hpp>

int main() {
    threadmaxx::editor::test::ScopedEngine env;

    threadmaxx::editor::EditorSession s1{env.engine()};
    CHECK(&s1.engine() == &env.engine());
    CHECK(s1.id().valid());

    threadmaxx::editor::EditorSession s2{env.engine()};
    CHECK(s2.id().valid());
    CHECK(!(s1.id() == s2.id()));

    EXIT_WITH_RESULT();
}
