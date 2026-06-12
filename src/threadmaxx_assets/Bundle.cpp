#include "threadmaxx_assets/bundle.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "threadmaxx_assets/detail/io.hpp"

namespace threadmaxx::assets {

namespace {

void putBytes(std::vector<std::byte>& dst, const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    dst.insert(dst.end(), b, b + n);
}

template <class T>
void putLE(std::vector<std::byte>& dst, T v) {
    putBytes(dst, &v, sizeof(T));
}

void putString(std::vector<std::byte>& dst, const std::string& s) {
    putLE<std::uint32_t>(dst, static_cast<std::uint32_t>(s.size()));
    putBytes(dst, s.data(), s.size());
}

void putBlob(std::vector<std::byte>& dst, const std::vector<std::byte>& src) {
    putLE<std::uint64_t>(dst, static_cast<std::uint64_t>(src.size()));
    putBytes(dst, src.data(), src.size());
}

// Reader helpers.

struct Reader {
    const std::byte* p;
    const std::byte* end;
    bool error{false};

    bool require(std::size_t n) noexcept {
        if (p + n > end) {
            error = true;
            return false;
        }
        return true;
    }

    template <class T>
    T readLE() noexcept {
        T v{};
        if (!require(sizeof(T))) return v;
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }

    std::string readString() {
        const auto n = readLE<std::uint32_t>();
        if (error) return {};
        if (!require(n)) return {};
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        return s;
    }

    std::vector<std::byte> readBlob() {
        const auto n = readLE<std::uint64_t>();
        if (error) return {};
        if (!require(n)) return {};
        std::vector<std::byte> v(reinterpret_cast<const std::byte*>(p),
                                 reinterpret_cast<const std::byte*>(p + n));
        p += n;
        return v;
    }
};

// Mesh serialization (vertices / indices / submeshes / aabb / sourcePath).

void writeMesh(std::vector<std::byte>& dst, const MeshData& m) {
    putString(dst, m.sourcePath);
    putLE<std::uint64_t>(dst, static_cast<std::uint64_t>(m.vertices.size()));
    putBytes(dst, m.vertices.data(), m.vertices.size() * sizeof(MeshVertex));
    putLE<std::uint64_t>(dst, static_cast<std::uint64_t>(m.indices.size()));
    putBytes(dst, m.indices.data(), m.indices.size() * sizeof(std::uint32_t));
    putLE<std::uint64_t>(dst, static_cast<std::uint64_t>(m.submeshes.size()));
    for (const auto& s : m.submeshes) {
        putLE<std::uint32_t>(dst, s.firstIndex);
        putLE<std::uint32_t>(dst, s.indexCount);
        putLE<std::uint32_t>(dst, s.materialIndex);
        putString(dst, s.materialName);
    }
    putBytes(dst, m.aabb.min, sizeof(m.aabb.min));
    putBytes(dst, m.aabb.max, sizeof(m.aabb.max));
}

MeshData readMesh(Reader& r) {
    MeshData m;
    m.sourcePath = r.readString();
    const auto nv = r.readLE<std::uint64_t>();
    if (!r.require(nv * sizeof(MeshVertex))) return m;
    m.vertices.resize(nv);
    std::memcpy(m.vertices.data(), r.p, nv * sizeof(MeshVertex));
    r.p += nv * sizeof(MeshVertex);

    const auto ni = r.readLE<std::uint64_t>();
    if (!r.require(ni * sizeof(std::uint32_t))) return m;
    m.indices.resize(ni);
    std::memcpy(m.indices.data(), r.p, ni * sizeof(std::uint32_t));
    r.p += ni * sizeof(std::uint32_t);

    const auto ns = r.readLE<std::uint64_t>();
    m.submeshes.reserve(ns);
    for (std::uint64_t i = 0; i < ns; ++i) {
        MeshSubmesh sm;
        sm.firstIndex    = r.readLE<std::uint32_t>();
        sm.indexCount    = r.readLE<std::uint32_t>();
        sm.materialIndex = r.readLE<std::uint32_t>();
        sm.materialName  = r.readString();
        m.submeshes.push_back(std::move(sm));
    }

    if (r.require(sizeof(m.aabb.min))) {
        std::memcpy(m.aabb.min, r.p, sizeof(m.aabb.min));
        r.p += sizeof(m.aabb.min);
    }
    if (r.require(sizeof(m.aabb.max))) {
        std::memcpy(m.aabb.max, r.p, sizeof(m.aabb.max));
        r.p += sizeof(m.aabb.max);
    }
    return m;
}

void writeTexture(std::vector<std::byte>& dst, const TextureData& t) {
    putString(dst, t.sourcePath);
    putLE<std::uint32_t>(dst, t.width);
    putLE<std::uint32_t>(dst, t.height);
    putLE<std::uint8_t>(dst, static_cast<std::uint8_t>(t.format));
    putLE<std::uint8_t>(dst, t.srgb ? std::uint8_t{1} : std::uint8_t{0});
    putBlob(dst, t.pixels);
}

TextureData readTexture(Reader& r) {
    TextureData t;
    t.sourcePath = r.readString();
    t.width      = r.readLE<std::uint32_t>();
    t.height     = r.readLE<std::uint32_t>();
    t.format     = static_cast<PixelFormat>(r.readLE<std::uint8_t>());
    t.srgb       = r.readLE<std::uint8_t>() != 0;
    t.pixels     = r.readBlob();
    return t;
}

void writeAudio(std::vector<std::byte>& dst, const AudioClipData& a) {
    putString(dst, a.sourcePath);
    putLE<std::uint32_t>(dst, a.sampleRate);
    putLE<std::uint16_t>(dst, a.channels);
    putLE<std::uint8_t>(dst, static_cast<std::uint8_t>(a.format));
    putBlob(dst, a.samples);
}

AudioClipData readAudio(Reader& r) {
    AudioClipData a;
    a.sourcePath = r.readString();
    a.sampleRate = r.readLE<std::uint32_t>();
    a.channels   = r.readLE<std::uint16_t>();
    a.format     = static_cast<SampleFormat>(r.readLE<std::uint8_t>());
    a.samples    = r.readBlob();
    return a;
}

void writeFont(std::vector<std::byte>& dst, const FontAtlas& f) {
    putString(dst, f.sourcePath);
    putString(dst, f.fontName);
    putLE<std::uint16_t>(dst, f.fontSize);
    putLE<std::uint16_t>(dst, f.lineHeight);
    putLE<std::uint16_t>(dst, f.base);

    putLE<std::uint64_t>(dst, static_cast<std::uint64_t>(f.pages.size()));
    for (const auto& page : f.pages) writeTexture(dst, page);

    putLE<std::uint64_t>(dst, static_cast<std::uint64_t>(f.glyphs.size()));
    putBytes(dst, f.glyphs.data(), f.glyphs.size() * sizeof(FontGlyph));

    putLE<std::uint64_t>(dst, static_cast<std::uint64_t>(f.kernings.size()));
    putBytes(dst, f.kernings.data(), f.kernings.size() * sizeof(FontKerning));
}

FontAtlas readFont(Reader& r) {
    FontAtlas f;
    f.sourcePath = r.readString();
    f.fontName   = r.readString();
    f.fontSize   = r.readLE<std::uint16_t>();
    f.lineHeight = r.readLE<std::uint16_t>();
    f.base       = r.readLE<std::uint16_t>();

    const auto np = r.readLE<std::uint64_t>();
    for (std::uint64_t i = 0; i < np; ++i) {
        f.pages.push_back(readTexture(r));
    }

    const auto ng = r.readLE<std::uint64_t>();
    if (!r.require(ng * sizeof(FontGlyph))) return f;
    f.glyphs.resize(ng);
    std::memcpy(f.glyphs.data(), r.p, ng * sizeof(FontGlyph));
    r.p += ng * sizeof(FontGlyph);

    const auto nk = r.readLE<std::uint64_t>();
    if (!r.require(nk * sizeof(FontKerning))) return f;
    f.kernings.resize(nk);
    std::memcpy(f.kernings.data(), r.p, nk * sizeof(FontKerning));
    r.p += nk * sizeof(FontKerning);

    return f;
}

} // namespace

AssetResult<std::vector<std::byte>> writeBundle(const Bundle& b) {
    std::vector<std::byte> out;
    putLE<std::uint32_t>(out, kBundleMagic);
    putLE<std::uint32_t>(out, kBundleVersion);
    putLE<std::uint32_t>(out, static_cast<std::uint32_t>(b.meshes.size()));
    putLE<std::uint32_t>(out, static_cast<std::uint32_t>(b.textures.size()));
    putLE<std::uint32_t>(out, static_cast<std::uint32_t>(b.audio.size()));
    putLE<std::uint32_t>(out, static_cast<std::uint32_t>(b.fonts.size()));

    for (const auto& [name, m] : b.meshes) {
        putString(out, name);
        writeMesh(out, m);
    }
    for (const auto& [name, t] : b.textures) {
        putString(out, name);
        writeTexture(out, t);
    }
    for (const auto& [name, a] : b.audio) {
        putString(out, name);
        writeAudio(out, a);
    }
    for (const auto& [name, f] : b.fonts) {
        putString(out, name);
        writeFont(out, f);
    }

    return AssetResult<std::vector<std::byte>>::success(std::move(out));
}

AssetResult<Bundle> readBundle(std::span<const std::byte> bytes) {
    Reader r{bytes.data(), bytes.data() + bytes.size(), false};
    const auto magic = r.readLE<std::uint32_t>();
    if (r.error || magic != kBundleMagic) {
        return AssetResult<Bundle>::failure(ErrorCode::BadMagic, "bundle magic");
    }
    const auto version = r.readLE<std::uint32_t>();
    if (version != kBundleVersion) {
        return AssetResult<Bundle>::failure(
            ErrorCode::UnsupportedVersion, "bundle version != 1");
    }
    const auto nm = r.readLE<std::uint32_t>();
    const auto nt = r.readLE<std::uint32_t>();
    const auto na = r.readLE<std::uint32_t>();
    const auto nf = r.readLE<std::uint32_t>();

    Bundle b;
    b.meshes.reserve(nm);
    b.textures.reserve(nt);
    b.audio.reserve(na);
    b.fonts.reserve(nf);

    for (std::uint32_t i = 0; i < nm; ++i) {
        auto name = r.readString();
        auto m = readMesh(r);
        if (r.error) {
            return AssetResult<Bundle>::failure(
                ErrorCode::Truncated, "bundle mesh truncated");
        }
        b.meshes.emplace_back(std::move(name), std::move(m));
    }
    for (std::uint32_t i = 0; i < nt; ++i) {
        auto name = r.readString();
        auto t = readTexture(r);
        if (r.error) {
            return AssetResult<Bundle>::failure(
                ErrorCode::Truncated, "bundle texture truncated");
        }
        b.textures.emplace_back(std::move(name), std::move(t));
    }
    for (std::uint32_t i = 0; i < na; ++i) {
        auto name = r.readString();
        auto a = readAudio(r);
        if (r.error) {
            return AssetResult<Bundle>::failure(
                ErrorCode::Truncated, "bundle audio truncated");
        }
        b.audio.emplace_back(std::move(name), std::move(a));
    }
    for (std::uint32_t i = 0; i < nf; ++i) {
        auto name = r.readString();
        auto f = readFont(r);
        if (r.error) {
            return AssetResult<Bundle>::failure(
                ErrorCode::Truncated, "bundle font truncated");
        }
        b.fonts.emplace_back(std::move(name), std::move(f));
    }

    return AssetResult<Bundle>::success(std::move(b));
}

AssetResult<Bundle> readBundleFromFile(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<Bundle>::failure(bytes.code, std::move(bytes.message));
    }
    return readBundle(bytes.value);
}

BundleMount mountBundleInto(AssetRegistry& reg, const Bundle& b) {
    BundleMount out;
    out.meshes.reserve(b.meshes.size());
    out.textures.reserve(b.textures.size());
    out.audio.reserve(b.audio.size());
    out.fonts.reserve(b.fonts.size());

    for (const auto& [name, m] : b.meshes)   out.meshes.push_back(reg.addMesh(name, m));
    for (const auto& [name, t] : b.textures) out.textures.push_back(reg.addTexture(name, t));
    for (const auto& [name, a] : b.audio)    out.audio.push_back(reg.addAudio(name, a));
    for (const auto& [name, f] : b.fonts)    out.fonts.push_back(reg.addFont(name, f));
    return out;
}

} // namespace threadmaxx::assets
