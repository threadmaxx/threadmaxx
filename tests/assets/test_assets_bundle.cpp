#include "Check.hpp"

#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "threadmaxx_assets/bundle.hpp"
#include "threadmaxx_assets/registry.hpp"

using namespace threadmaxx::assets;

namespace {

Bundle buildSampleBundle() {
    Bundle b;

    MeshData m;
    m.sourcePath = "cube.obj";
    m.vertices.push_back(MeshVertex{{0,0,0},{0,0,1},{0,0}});
    m.vertices.push_back(MeshVertex{{1,0,0},{0,0,1},{1,0}});
    m.vertices.push_back(MeshVertex{{0,1,0},{0,0,1},{0,1}});
    m.indices = {0u, 1u, 2u};
    MeshSubmesh sm;
    sm.firstIndex = 0;
    sm.indexCount = 3;
    sm.materialName = "default";
    m.submeshes.push_back(std::move(sm));
    m.aabb.min[0] = 0.0f; m.aabb.min[1] = 0.0f; m.aabb.min[2] = 0.0f;
    m.aabb.max[0] = 1.0f; m.aabb.max[1] = 1.0f; m.aabb.max[2] = 0.0f;
    b.meshes.emplace_back("triangle", std::move(m));

    TextureData t;
    t.width = 2; t.height = 2; t.format = PixelFormat::RGBA8; t.srgb = true;
    t.sourcePath = "tex.png";
    t.pixels = std::vector<std::byte>(16, std::byte{0x55});
    b.textures.emplace_back("texA", std::move(t));

    AudioClipData a;
    a.sampleRate = 48000;
    a.channels = 1;
    a.format = SampleFormat::PcmS16;
    a.samples = std::vector<std::byte>(64, std::byte{0xAA});
    b.audio.emplace_back("sfx", std::move(a));

    FontAtlas f;
    f.fontName = "Test";
    f.fontSize = 12; f.lineHeight = 16; f.base = 10;
    f.glyphs.push_back(FontGlyph{65, 0, 0, 8, 12, 0, 0, 9, 0});
    f.kernings.push_back(FontKerning{65, 66, -1});
    b.fonts.emplace_back("ui-font", std::move(f));

    return b;
}

} // namespace

int main() {
    const auto b0 = buildSampleBundle();

    // Round-trip: serialize → deserialize → serialize is byte-identical.
    auto wr0 = writeBundle(b0);
    CHECK(wr0.ok());
    if (!wr0.ok()) EXIT_WITH_RESULT();

    auto rd = readBundle(wr0.value);
    CHECK(rd.ok());
    if (!rd.ok()) EXIT_WITH_RESULT();

    auto wr1 = writeBundle(rd.value);
    CHECK(wr1.ok());
    CHECK_EQ(wr0.value.size(), wr1.value.size());
    if (wr0.value.size() == wr1.value.size()) {
        const bool equal = std::memcmp(wr0.value.data(), wr1.value.data(),
                                       wr0.value.size()) == 0;
        CHECK(equal);
    }

    // Bad magic.
    std::vector<std::byte> bad(8, std::byte{0});
    auto badR = readBundle(bad);
    CHECK(!badR.ok());
    CHECK(badR.code == ErrorCode::BadMagic);

    // Bad version: write a bundle, then poke the version field.
    auto vbytes = wr0.value;
    if (vbytes.size() >= 8) {
        const std::uint32_t bumped = 999;
        std::memcpy(vbytes.data() + 4, &bumped, sizeof(bumped));
    }
    auto verR = readBundle(vbytes);
    CHECK(!verR.ok());
    CHECK(verR.code == ErrorCode::UnsupportedVersion);

    // Mount: every asset is reachable via findX while the mount holder is alive.
    {
        AssetRegistry reg;
        auto mount = mountBundleInto(reg, rd.value);
        CHECK_EQ(mount.meshes.size(),   std::size_t{1});
        CHECK_EQ(mount.textures.size(), std::size_t{1});
        CHECK_EQ(mount.audio.size(),    std::size_t{1});
        CHECK_EQ(mount.fonts.size(),    std::size_t{1});

        auto mh = reg.findMesh("triangle");
        auto th = reg.findTexture("texA");
        auto ah = reg.findAudio("sfx");
        auto fh = reg.findFont("ui-font");
        CHECK(mh.valid());
        CHECK(th.valid());
        CHECK(ah.valid());
        CHECK(fh.valid());
        CHECK_EQ(mh->indices.size(), std::size_t{3});
        CHECK_EQ(th->pixels.size(), std::size_t{16});
        CHECK_EQ(ah->channels,      std::uint16_t{1});
        CHECK_EQ(fh->fontSize,      std::uint16_t{12});
    }

    EXIT_WITH_RESULT();
}
