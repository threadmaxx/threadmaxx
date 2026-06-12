// threadmaxx_assets v1.0 headless smoke. Loads the bundled cube.obj,
// builds a tiny in-memory texture / audio / font, writes a bundle,
// reads it back, and asserts the round-trip.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <threadmaxx_assets/bundle.hpp>
#include <threadmaxx_assets/data/audio.hpp>
#include <threadmaxx_assets/data/font.hpp>
#include <threadmaxx_assets/data/texture.hpp>
#include <threadmaxx_assets/loaders/obj.hpp>
#include <threadmaxx_assets/registry.hpp>
#include <threadmaxx_assets/version.hpp>

#ifndef THREADMAXX_ASSETS_DEMO_CUBE_PATH
#  define THREADMAXX_ASSETS_DEMO_CUBE_PATH ""
#endif

using namespace threadmaxx::assets;

int main() {
    std::printf("threadmaxx_assets v%s\n", std::string(version_string()).c_str());

    const std::string cubePath = THREADMAXX_ASSETS_DEMO_CUBE_PATH;
    if (cubePath.empty()) {
        std::fprintf(stderr, "no cube path defined\n");
        return 1;
    }

    auto mesh = loadObj(cubePath);
    if (!mesh.ok()) {
        std::fprintf(stderr, "OBJ load failed: %s\n", mesh.message.c_str());
        return 1;
    }
    std::printf("loaded cube.obj: %zu vertices, %zu indices\n",
                mesh.value.vertices.size(), mesh.value.indices.size());

    TextureData tex;
    tex.width = 2; tex.height = 2; tex.format = PixelFormat::RGBA8;
    tex.pixels = std::vector<std::byte>(16, std::byte{0xAB});

    AudioClipData clip;
    clip.sampleRate = 48000;
    clip.channels = 1;
    clip.format = SampleFormat::PcmS16;
    clip.samples = std::vector<std::byte>(64, std::byte{0});

    FontAtlas font;
    font.fontName = "DemoFont";
    font.fontSize = 16;
    font.lineHeight = 20;
    font.base = 14;

    // Bundle + round-trip.
    Bundle b;
    b.meshes.emplace_back("cube",   std::move(mesh.value));
    b.textures.emplace_back("tex0", std::move(tex));
    b.audio.emplace_back("clip0",   std::move(clip));
    b.fonts.emplace_back("font0",   std::move(font));

    auto wr = writeBundle(b);
    if (!wr.ok()) {
        std::fprintf(stderr, "writeBundle failed\n");
        return 1;
    }
    std::printf("bundle: %zu bytes\n", wr.value.size());

    auto rd = readBundle(wr.value);
    if (!rd.ok()) {
        std::fprintf(stderr, "readBundle failed: %s\n", rd.message.c_str());
        return 1;
    }

    // Mount into a registry; AssetHandles in BundleMount keep slots alive.
    AssetRegistry reg;
    auto mount = mountBundleInto(reg, rd.value);
    std::printf("mounted: %zu meshes, %zu textures, %zu audio, %zu fonts\n",
                mount.meshes.size(), mount.textures.size(),
                mount.audio.size(),  mount.fonts.size());

    // Lookup spot-check.
    auto h = reg.findMesh("cube");
    if (!h.valid()) {
        std::fprintf(stderr, "findMesh post-mount failed\n");
        return 1;
    }
    std::printf("post-mount cube has %zu indices\n", h->indices.size());

    // Round-trip byte equality.
    auto wr2 = writeBundle(rd.value);
    if (!wr2.ok() || wr2.value.size() != wr.value.size() ||
        std::memcmp(wr.value.data(), wr2.value.data(), wr.value.size()) != 0) {
        std::fprintf(stderr, "bundle round-trip not byte-identical\n");
        return 1;
    }
    std::printf("bundle round-trip OK\n");

    std::printf("assets_demo done\n");
    return 0;
}
