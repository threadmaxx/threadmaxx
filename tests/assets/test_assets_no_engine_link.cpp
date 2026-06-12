/// @file test_assets_no_engine_link.cpp
/// @brief Pins the engine-decoupling invariant: every public
/// `threadmaxx_assets` header compiles without dragging in any
/// `threadmaxx/` core header. Mirrors the spec's §6.11 contract.

#include "Check.hpp"

#include "threadmaxx_assets/threadmaxx_assets.hpp"
#include "threadmaxx_assets/config.hpp"
#include "threadmaxx_assets/data/audio.hpp"
#include "threadmaxx_assets/data/font.hpp"
#include "threadmaxx_assets/data/mesh.hpp"
#include "threadmaxx_assets/data/texture.hpp"
#include "threadmaxx_assets/detail/io.hpp"
#include "threadmaxx_assets/types.hpp"
#include "threadmaxx_assets/version.hpp"

#if defined(THREADMAXX_VERSION_HPP)               \
    || defined(THREADMAXX_ENGINE_HPP)             \
    || defined(THREADMAXX_WORLD_HPP)              \
    || defined(THREADMAXX_COMPONENTS_HPP)         \
    || defined(THREADMAXX_GAME_HPP)               \
    || defined(THREADMAXX_SYSTEM_HPP)
#  error "threadmaxx_assets must not transitively include core engine headers"
#endif

int main() {
    using namespace threadmaxx::assets;
    (void)kInitialMeshVertexCapacity;
    MeshData    m{};
    TextureData t{};
    AudioClipData a{};
    FontAtlas   f{};
    (void)m; (void)t; (void)a; (void)f;
    CHECK_EQ(version_string(), "1.0.0");
    EXIT_WITH_RESULT();
}
