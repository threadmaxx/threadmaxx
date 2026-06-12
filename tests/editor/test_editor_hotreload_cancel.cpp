/// @file test_editor_hotreload_cancel.cpp
/// @brief E4 — cancelReload(path) removes the path from pending without
/// crashing the loader / controller.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/hotreload.hpp>

#include <threadmaxx/Resource.hpp>

namespace {

struct Texture { int width; int height; };

struct NoopLoader final : threadmaxx::IResourceLoader {
    void update(threadmaxx::Engine&) override {}
};

} // namespace

int main() {
    threadmaxx::editor::test::ScopedEngine env;

    env.engine().addResourceLoader(std::make_unique<NoopLoader>());
    auto handle = env.engine().resources()
                      .addRefCounted<Texture>(Texture{256, 256});

    threadmaxx::editor::HotReloadController ctl{env.engine()};
    ctl.trackResource(handle.id(), "diffuse.png");

    auto r = ctl.requestReload({"diffuse.png", false});
    CHECK(r.ok);
    CHECK_EQ(ctl.pendingReloads().size(), 1u);

    ctl.cancelReload("diffuse.png");
    CHECK_EQ(ctl.pendingReloads().size(), 0u);

    // Idempotent on a non-pending path.
    ctl.cancelReload("diffuse.png");
    ctl.cancelReload("never-pending.png");
    CHECK_EQ(ctl.pendingReloads().size(), 0u);

    env.engine().step();
    EXIT_WITH_RESULT();
}
