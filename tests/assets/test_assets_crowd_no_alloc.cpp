/// @file test_assets_crowd_no_alloc.cpp
/// @brief v1.0 close-out zero-allocation gate. 100 meshes / 100 textures /
/// 100 handle copies per "frame" over 100 measured frames after a 5-frame
/// warmup. Tracking allocator confirms zero heap traffic on the steady-
/// state polling path (findX / refCount / pump-idle).

#include "Check.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "threadmaxx_assets/async_loader.hpp"
#include "threadmaxx_assets/data/mesh.hpp"
#include "threadmaxx_assets/data/texture.hpp"
#include "threadmaxx_assets/registry.hpp"

namespace {

std::atomic<bool> g_track{false};
std::atomic<std::uint64_t> g_allocCount{0};
std::atomic<std::uint64_t> g_freeCount{0};

} // namespace

void* operator new(std::size_t n) {
    if (g_track.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t n) {
    if (g_track.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept {
    if (p && g_track.load(std::memory_order_relaxed)) {
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}
void operator delete[](void* p) noexcept {
    if (p && g_track.load(std::memory_order_relaxed)) {
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    if (p && g_track.load(std::memory_order_relaxed)) {
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    if (p && g_track.load(std::memory_order_relaxed)) {
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

using namespace threadmaxx::assets;

int main() {
    AssetRegistry reg;
    AsyncLoader   async(reg, 1);

    // Inject 100 meshes + 100 textures via addX so we don't depend on
    // disk I/O. Each name is a short distinct string we'll re-query
    // every frame; the lookup happens via findX which canonicalizes the
    // key string. We pre-warm the canonical-path cache by querying each
    // name once before the tracking window.
    std::vector<std::string> meshNames, texNames;
    meshNames.reserve(100);
    texNames.reserve(100);
    for (int i = 0; i < 100; ++i) {
        meshNames.push_back("mesh_" + std::to_string(i));
        texNames.push_back("tex_"   + std::to_string(i));
    }

    for (const auto& n : meshNames) {
        MeshData m;
        m.vertices.push_back(MeshVertex{{0,0,0},{0,0,1},{0,0}});
        m.indices = {0u};
        auto h = reg.addMesh(n, std::move(m));
        CHECK(h.valid());
    }
    for (const auto& n : texNames) {
        TextureData t;
        t.width = 1; t.height = 1; t.format = PixelFormat::RGBA8;
        t.pixels = std::vector<std::byte>(4, std::byte{0});
        auto h = reg.addTexture(n, std::move(t));
        CHECK(h.valid());
    }

    // Hold the BundleMount-style refs so we don't dedup-evict.
    std::vector<AssetHandle<MeshData>>    meshHandles;
    std::vector<AssetHandle<TextureData>> texHandles;
    meshHandles.reserve(100);
    texHandles.reserve(100);
    for (const auto& n : meshNames) meshHandles.push_back(reg.findMesh(n));
    for (const auto& n : texNames)  texHandles.push_back(reg.findTexture(n));

    auto perFrame = [&]() {
        // 100 meshes × findX (no I/O, no alloc — keys are heap-allocated
        // std::string canonical lookups, BUT findX only allocates if
        // weakly_canonical hits the disk; for non-existing paths it
        // returns the input string in-place under sso. Length<23 keeps
        // us on SSO for both gcc libstdc++ and libc++.)
        for (const auto& n : meshNames) {
            (void)reg.findMesh(n);
        }
        for (const auto& n : texNames) {
            (void)reg.findTexture(n);
        }
        // 100 handle copies — pure atomic increments.
        for (int i = 0; i < 100; ++i) {
            AssetHandle<MeshData> copy = meshHandles[static_cast<std::size_t>(i)];
            (void)copy.valid();
        }
        // Empty pump fast path.
        async.pump();
    };

    // Warmup: 5 frames untracked. Touches every keyToId entry so future
    // canonical lookups hit it.
    for (int f = 0; f < 5; ++f) perFrame();

    g_allocCount.store(0);
    g_freeCount.store(0);
    g_track.store(true);
    for (int f = 0; f < 100; ++f) perFrame();
    g_track.store(false);

    const auto allocs = g_allocCount.load();
    const auto frees  = g_freeCount.load();
    std::fprintf(stderr, "tracked frames=100 allocs=%llu frees=%llu\n",
                 static_cast<unsigned long long>(allocs),
                 static_cast<unsigned long long>(frees));
    CHECK_EQ(allocs, 0ull);
    CHECK_EQ(frees,  0ull);

    EXIT_WITH_RESULT();
}
