/// @file test_editor_layout_roundtrip.cpp
/// @brief E10 — save layout, load into a fresh manager, all panels
/// match.

#include "Check.hpp"

#include <threadmaxx_editor/layout.hpp>

#include <sstream>

int main() {
    threadmaxx::editor::LayoutState s{};
    s.selectedPanel = "Inspector";
    s.dockJson = "{\"a\":1}";
    s.panels["Hierarchy"] = true;
    s.panels["Properties"] = true;
    s.panels["Console"] = false;

    threadmaxx::editor::LayoutManager mgr{s};

    std::stringstream out;
    mgr.save(out);

    threadmaxx::editor::LayoutManager loaded{};
    std::stringstream in(out.str());
    CHECK(loaded.load(in));
    const auto& got = loaded.state();

    CHECK(got.selectedPanel == "Inspector");
    CHECK(got.dockJson == "{\"a\":1}");
    CHECK_EQ(got.panels.size(), 3u);
    CHECK(got.panels.at("Hierarchy") == true);
    CHECK(got.panels.at("Properties") == true);
    CHECK(got.panels.at("Console") == false);

    EXIT_WITH_RESULT();
}
