/// @file test_editor_hotreload_request.cpp
/// @brief E4 — requestReload({"foo.png"}) routes through
/// markResourceStale; a fake loader observes the markStale call.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/hotreload.hpp>

#include <threadmaxx/Resource.hpp>

namespace {

struct Texture { int width; int height; };

struct StaleObservingLoader final : threadmaxx::IResourceLoader {
    std::uint32_t lastIndex{0};
    std::uint32_t lastGeneration{0};
    int markStaleCount{0};

    void update(threadmaxx::Engine&) override {}
    void markStale(std::uint32_t index,
                   std::uint32_t generation,
                   std::type_index) override {
        lastIndex = index;
        lastGeneration = generation;
        ++markStaleCount;
    }
};

} // namespace

int main() {
    threadmaxx::editor::test::ScopedEngine env;

    auto* loader = static_cast<StaleObservingLoader*>(
        env.engine().addResourceLoader(
            std::make_unique<StaleObservingLoader>()));

    auto handle = env.engine().resources()
                      .addRefCounted<Texture>(Texture{256, 256});

    threadmaxx::editor::HotReloadController ctl{env.engine()};
    ctl.trackResource(handle.id(), "diffuse.png");
    CHECK_EQ(ctl.trackedCount(), 1u);

    auto result = ctl.requestReload({"diffuse.png", false});
    CHECK(result.ok);

    CHECK_EQ(loader->markStaleCount, 1);
    CHECK_EQ(loader->lastIndex, handle.id().index);
    CHECK_EQ(loader->lastGeneration, handle.id().generation);

    const auto pending = ctl.pendingReloads();
    CHECK_EQ(pending.size(), 1u);
    CHECK(pending[0] == "diffuse.png");

    // Untracked paths return ok=false.
    auto rejected = ctl.requestReload({"bogus.png", false});
    CHECK(!rejected.ok);

    EXIT_WITH_RESULT();
}
