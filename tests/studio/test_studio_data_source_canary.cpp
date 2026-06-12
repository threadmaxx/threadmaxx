/// @file test_studio_data_source_canary.cpp
/// @brief `IStudioDataSource` is a pure-virtual interface; every
/// optional accessor returns `std::nullopt` from the default impl;
/// `submitCommand()` defaults to `false`. Concrete impls in ST4 / M7
/// override the surfaces they actually back.

#include "Check.hpp"

#include <threadmaxx_studio/data_source.hpp>

#include <type_traits>

namespace {

class MinimalSource : public threadmaxx::studio::IStudioDataSource {
public:
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    static_assert(std::is_abstract_v<threadmaxx::studio::IStudioDataSource>,
                  "IStudioDataSource must be pure-virtual");
    static_assert(std::has_virtual_destructor_v<threadmaxx::studio::IStudioDataSource>,
                  "IStudioDataSource must have a virtual destructor");

    MinimalSource src;
    CHECK(src.mode() == threadmaxx::studio::AttachMode::Direct);

    // Every default optional accessor returns nullopt.
    CHECK(!src.engineSnapshot().has_value());
    CHECK(!src.animationStats().has_value());
    CHECK(!src.audioStats().has_value());
    CHECK(!src.inputStats().has_value());
    CHECK(!src.assetsStats().has_value());
    CHECK(!src.uiStats().has_value());
    CHECK(!src.navmeshStats().has_value());
    CHECK(!src.physicsStats().has_value());
    CHECK(!src.reflectStats().has_value());
    CHECK(!src.networkStats().has_value());
    CHECK(!src.migrationStats().has_value());

    // Default mutation path rejects submissions.
    CHECK(!src.submitCommand("noop"));

    // AttachMode enum surface.
    CHECK(static_cast<int>(threadmaxx::studio::AttachMode::Direct) == 0);
    CHECK(static_cast<int>(threadmaxx::studio::AttachMode::Remote) == 1);

    EXIT_WITH_RESULT();
}
