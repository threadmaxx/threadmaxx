#pragma once

/// @file StudioTestFixture.hpp
/// @brief Shared studio-test scaffolding — a `ScopedSession` RAII
/// that bundles a `ScopedEngine` + an `editor::EditorSession`.
/// Reuses the editor fixture's no-op `IGame` setup so studio tests
/// don't reinvent that wheel.

#include "editor/EditorTestFixture.hpp"

#include <threadmaxx_editor/session.hpp>
#include <threadmaxx_studio/panel.hpp>

#include <string>
#include <string_view>

namespace threadmaxx::studio::test {

/// @brief RAII engine + editor session for studio tests. The engine
/// and the session live together for the test's lifetime; the session
/// holds the engine reference.
class ScopedSession {
public:
    ScopedSession() : session_(env_.engine()) {}

    threadmaxx::Engine&             engine()        noexcept { return env_.engine(); }
    threadmaxx::editor::EditorSession& session()    noexcept { return session_; }

private:
    threadmaxx::editor::test::ScopedEngine env_{};
    threadmaxx::editor::EditorSession session_;
};

/// @brief Pure-virtual stub panel for tests that need an `IStudioPanel`
/// instance with a specific id.
class StubPanel : public IStudioPanel {
public:
    explicit StubPanel(std::string_view id, std::string_view title = "Stub")
        : id_(id), title_(title) {}

    std::string_view id() const noexcept override { return id_; }
    std::string_view title() const noexcept override { return title_; }
    void render(threadmaxx::editor::IEditorBackend&,
                IStudioDataSource&) override { ++renderCount_; }

    int renderCount_ = 0;

private:
    std::string id_;
    std::string title_;
};

} // namespace threadmaxx::studio::test
