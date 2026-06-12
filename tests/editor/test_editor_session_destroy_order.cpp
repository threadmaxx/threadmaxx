/// @file test_editor_session_destroy_order.cpp
/// @brief E1 — destroying the session before the engine is safe.
/// Installing/uninstalling a backend fires its lifecycle hooks the
/// right number of times.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/session.hpp>

#include <string_view>

namespace {

struct CountingBackend final : threadmaxx::editor::IEditorBackend {
    int initCalls{0};
    int shutdownCalls{0};

    bool initialize() override { ++initCalls; return true; }
    void shutdown() override { ++shutdownCalls; }
    void beginFrame() override {}
    void endFrame() override {}
    void drawText(std::string_view, float, float) override {}
    void drawRect(float, float, float, float) override {}
};

} // namespace

int main() {
    threadmaxx::editor::test::ScopedEngine env;

    {
        threadmaxx::editor::EditorSession s{env.engine()};
        // Destroying the session before installing a backend is safe.
    }

    CountingBackend b1;
    {
        threadmaxx::editor::EditorSession s{env.engine()};
        CHECK(s.setBackend(&b1));
        CHECK_EQ(b1.initCalls, 1);
        // session dtor fires backend->shutdown() exactly once.
    }
    CHECK_EQ(b1.shutdownCalls, 1);

    CountingBackend b2;
    {
        threadmaxx::editor::EditorSession s{env.engine()};
        CHECK(s.setBackend(&b2));
        CHECK(s.setBackend(nullptr));
        CHECK_EQ(b2.shutdownCalls, 1);
        // Now session dtor does NOT call shutdown() again.
    }
    CHECK_EQ(b2.shutdownCalls, 1);

    EXIT_WITH_RESULT();
}
