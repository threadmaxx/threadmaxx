#include "Check.hpp"

#include <type_traits>

#include "threadmaxx_assets/data/audio.hpp"
#include "threadmaxx_assets/data/font.hpp"
#include "threadmaxx_assets/data/mesh.hpp"
#include "threadmaxx_assets/data/texture.hpp"
#include "threadmaxx_assets/types.hpp"

using namespace threadmaxx::assets;

int main() {
    // MeshVertex is the load-bearing hot-path POD; size and triviality
    // gate downstream packing assumptions.
    static_assert(sizeof(MeshVertex) == 32);
    static_assert(std::is_trivially_copyable_v<MeshVertex>);
    static_assert(std::is_trivially_destructible_v<MeshVertex>);

    static_assert(std::is_trivially_copyable_v<Aabb>);
    static_assert(std::is_trivially_copyable_v<FontGlyph>);
    static_assert(std::is_trivially_copyable_v<FontKerning>);

    // POD enum sizes.
    static_assert(sizeof(AssetType)   == 1);
    static_assert(sizeof(ErrorCode)   == 2);
    static_assert(sizeof(PixelFormat) == 1);
    static_assert(sizeof(SampleFormat) == 1);

    // bytesPerPixel / bytesPerSample sanity.
    CHECK_EQ(bytesPerPixel(PixelFormat::R8),    1u);
    CHECK_EQ(bytesPerPixel(PixelFormat::RGB8),  3u);
    CHECK_EQ(bytesPerPixel(PixelFormat::RGBA8), 4u);
    CHECK_EQ(bytesPerPixel(PixelFormat::Unknown), 0u);
    CHECK_EQ(bytesPerSample(SampleFormat::PcmS16), 2u);
    CHECK_EQ(bytesPerSample(SampleFormat::PcmF32), 4u);

    // AudioClipData helper round-trip.
    AudioClipData clip{};
    clip.sampleRate = 48000;
    clip.channels   = 2;
    clip.format     = SampleFormat::PcmF32;
    clip.samples.resize(48000u * 2u * 4u); // exactly 1 second
    CHECK_EQ(clip.sampleFrames(), 48000ull);
    CHECK(clip.durationSeconds() > 0.999);
    CHECK(clip.durationSeconds() < 1.001);

    // AssetResult success / failure path.
    auto ok = AssetResult<int>::success(7);
    CHECK(ok.ok());
    CHECK_EQ(ok.value, 7);

    auto err = AssetResult<int>::failure(ErrorCode::ParseError, "boom");
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::ParseError);
    CHECK_EQ(err.message, std::string("boom"));

    EXIT_WITH_RESULT();
}
