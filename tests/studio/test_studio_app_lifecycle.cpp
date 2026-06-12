/// @file test_studio_app_lifecycle.cpp
/// @brief ST2 — construct StudioApp over an EditorSession; start /
/// stop round-trip; destructor leaves the app stopped.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_studio/studio.hpp>

int main() {
    threadmaxx::studio::test::ScopedSession env{};

    threadmaxx::studio::StudioApp app{env.session()};
    CHECK(!app.running());
    CHECK(&app.session() == &env.session());

    CHECK(app.start());
    CHECK(app.running());

    // Idempotent.
    CHECK(app.start());
    CHECK(app.running());

    app.stop();
    CHECK(!app.running());

    // Idempotent on the other side too.
    app.stop();
    CHECK(!app.running());

    CHECK_EQ(app.panelHost().panelCount(), 0u);

    EXIT_WITH_RESULT();
}
