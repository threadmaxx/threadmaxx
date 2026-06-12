/// @file test_studio_layout_persists_via_editor.cpp
/// @brief ST2 — visibility shadow state round-trips through
/// `editor::LayoutManager`. Studio never owns a parallel layout
/// system; this pins that contract.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_editor/layout.hpp>
#include <threadmaxx_studio/studio.hpp>

#include <sstream>

int main() {
    threadmaxx::studio::test::ScopedSession env{};
    threadmaxx::studio::StudioApp app{env.session()};

    threadmaxx::studio::test::StubPanel p1{"engine.inspector", "Inspector"};
    threadmaxx::studio::test::StubPanel p2{"engine.profiler", "Profiler"};

    auto& host = app.panelHost();
    CHECK(host.registerPanel(&p1));
    CHECK(host.registerPanel(&p2));

    // Diverge from defaults so the round-trip has something to assert.
    host.setVisible("engine.inspector", true);
    host.setVisible("engine.profiler", false);

    threadmaxx::editor::LayoutManager mgr{};
    host.saveTo(mgr);

    CHECK_EQ(mgr.state().panels.size(), 2u);
    CHECK(mgr.state().panels.at("engine.inspector") == true);
    CHECK(mgr.state().panels.at("engine.profiler") == false);

    // Wire-roundtrip via the editor's serializer.
    std::stringstream wire;
    mgr.save(wire);

    threadmaxx::editor::LayoutManager mgrLoaded{};
    std::stringstream in(wire.str());
    CHECK(mgrLoaded.load(in));

    // Fresh host wired up with the same two panels in a different
    // visibility state, then restored from the wire.
    threadmaxx::studio::StudioApp app2{env.session()};
    auto& host2 = app2.panelHost();
    CHECK(host2.registerPanel(&p1));
    CHECK(host2.registerPanel(&p2));
    host2.setVisible("engine.inspector", false);
    host2.setVisible("engine.profiler", true);

    host2.restoreFrom(mgrLoaded);
    CHECK(host2.isVisible("engine.inspector") == true);
    CHECK(host2.isVisible("engine.profiler") == false);

    EXIT_WITH_RESULT();
}
