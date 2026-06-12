/// @file test_studio_panel_register_unregister.cpp
/// @brief ST2 — registerPanel / unregisterPanel / findPanel round-
/// trip. Double-register fails; unregister of unknown id returns
/// false; null panel pointer is rejected.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_studio/studio.hpp>

int main() {
    threadmaxx::studio::test::ScopedSession env{};
    threadmaxx::studio::StudioApp app{env.session()};

    threadmaxx::studio::test::StubPanel p1{"stub-id", "Stub Panel"};

    auto& host = app.panelHost();
    CHECK_EQ(host.panelCount(), 0u);
    CHECK(host.findPanel("stub-id") == nullptr);

    CHECK(host.registerPanel(&p1));
    CHECK_EQ(host.panelCount(), 1u);
    CHECK(host.findPanel("stub-id") == &p1);

    // Double-register on same id is rejected.
    CHECK(!host.registerPanel(&p1));
    CHECK_EQ(host.panelCount(), 1u);

    // Null pointer rejected.
    CHECK(!host.registerPanel(nullptr));

    // A different panel with a different id registers fine.
    threadmaxx::studio::test::StubPanel p2{"other-id"};
    CHECK(host.registerPanel(&p2));
    CHECK_EQ(host.panelCount(), 2u);
    CHECK(host.findPanel("other-id") == &p2);

    // Unregister of unknown id returns false.
    CHECK(!host.unregisterPanel("does-not-exist"));
    CHECK_EQ(host.panelCount(), 2u);

    // Real unregister succeeds; subsequent find returns null.
    CHECK(host.unregisterPanel("stub-id"));
    CHECK_EQ(host.panelCount(), 1u);
    CHECK(host.findPanel("stub-id") == nullptr);
    CHECK(host.findPanel("other-id") == &p2);

    EXIT_WITH_RESULT();
}
