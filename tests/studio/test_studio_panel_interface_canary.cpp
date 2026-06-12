/// @file test_studio_panel_interface_canary.cpp
/// @brief `IStudioPanel` is a pure-virtual interface and its load-
/// bearing virtuals exist. We pin the shape by defining a stub
/// implementation; if any required virtual goes away, this fails to
/// compile rather than silently regressing.

#include "Check.hpp"

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panel.hpp>

#include <string_view>
#include <type_traits>

namespace {

class StubDataSource : public threadmaxx::studio::IStudioDataSource {
public:
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

class StubPanel : public threadmaxx::studio::IStudioPanel {
public:
    std::string_view id() const noexcept override { return "stub"; }
    std::string_view title() const noexcept override { return "Stub"; }
    void render(threadmaxx::editor::IEditorBackend&,
                threadmaxx::studio::IStudioDataSource&) override {
        ++renderCount_;
    }
    void onAttachChanged(threadmaxx::studio::AttachMode m) override {
        lastMode_ = m;
    }

    int renderCount_ = 0;
    threadmaxx::studio::AttachMode lastMode_ =
        threadmaxx::studio::AttachMode::Direct;
};

} // namespace

int main() {
    static_assert(std::is_abstract_v<threadmaxx::studio::IStudioPanel>,
                  "IStudioPanel must be pure-virtual");
    static_assert(std::has_virtual_destructor_v<threadmaxx::studio::IStudioPanel>,
                  "IStudioPanel must have a virtual destructor");

    StubPanel panel;
    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    StubDataSource source;

    CHECK(panel.id() == "stub");
    CHECK(panel.title() == "Stub");

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.renderCount_, 1);

    panel.onAttachChanged(threadmaxx::studio::AttachMode::Remote);
    CHECK(panel.lastMode_ == threadmaxx::studio::AttachMode::Remote);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
