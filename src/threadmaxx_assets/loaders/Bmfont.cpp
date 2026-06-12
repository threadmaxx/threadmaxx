#include "threadmaxx_assets/loaders/bmfont.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "threadmaxx_assets/detail/io.hpp"
#include "threadmaxx_assets/loaders/png.hpp"

namespace threadmaxx::assets {

namespace {

// String / token utilities for the text variant. ---------------------------

bool isSpace(char c) noexcept { return c == ' ' || c == '\t'; }
bool isLineBreak(char c) noexcept { return c == '\n' || c == '\r'; }

std::string_view trimLeft(std::string_view s) noexcept {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    return s;
}

std::string_view nextToken(std::string_view& s) noexcept {
    s = trimLeft(s);
    if (s.empty()) return {};
    const auto begin = s.data();
    while (!s.empty() && !isSpace(s.front()) && !isLineBreak(s.front())) {
        s.remove_prefix(1);
    }
    return {begin, static_cast<std::size_t>(s.data() - begin)};
}

// Parse a `key=value` token; `value` may be a quoted string.
bool splitKV(std::string_view tok, std::string_view& key,
             std::string_view& val) noexcept {
    const auto eq = tok.find('=');
    if (eq == std::string_view::npos) return false;
    key = tok.substr(0, eq);
    val = tok.substr(eq + 1);
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val.remove_prefix(1);
        val.remove_suffix(1);
    }
    return true;
}

std::int64_t parseInt(std::string_view s) noexcept {
    if (s.empty()) return 0;
    return std::strtoll(std::string(s).c_str(), nullptr, 10);
}

// For text-format `chars first=N second=M amount=A` we collect pairs.
struct CharLineKV {
    std::int64_t id{}, x{}, y{}, w{}, h{};
    std::int64_t xOffset{}, yOffset{}, xAdvance{};
    std::int64_t page{};
};

void parseBmfontTextLine(std::string_view line, FontAtlas& out,
                         std::vector<std::string>& pageFiles) {
    line = trimLeft(line);
    if (line.empty()) return;
    auto rest = line;
    const auto tag = nextToken(rest);

    if (tag == "info") {
        for (;;) {
            const auto tok = nextToken(rest);
            if (tok.empty()) break;
            std::string_view k, v;
            if (!splitKV(tok, k, v)) continue;
            if (k == "face") out.fontName = std::string(v);
            else if (k == "size") out.fontSize =
                static_cast<std::uint16_t>(parseInt(v));
        }
    } else if (tag == "common") {
        for (;;) {
            const auto tok = nextToken(rest);
            if (tok.empty()) break;
            std::string_view k, v;
            if (!splitKV(tok, k, v)) continue;
            if (k == "lineHeight") out.lineHeight =
                static_cast<std::uint16_t>(parseInt(v));
            else if (k == "base") out.base =
                static_cast<std::uint16_t>(parseInt(v));
        }
    } else if (tag == "page") {
        std::int64_t id = 0;
        std::string file;
        for (;;) {
            const auto tok = nextToken(rest);
            if (tok.empty()) break;
            std::string_view k, v;
            if (!splitKV(tok, k, v)) continue;
            if (k == "id") id = parseInt(v);
            else if (k == "file") file = std::string(v);
        }
        if (id >= 0) {
            if (static_cast<std::size_t>(id) >= pageFiles.size()) {
                pageFiles.resize(static_cast<std::size_t>(id + 1));
            }
            pageFiles[static_cast<std::size_t>(id)] = std::move(file);
        }
    } else if (tag == "char") {
        CharLineKV c;
        for (;;) {
            const auto tok = nextToken(rest);
            if (tok.empty()) break;
            std::string_view k, v;
            if (!splitKV(tok, k, v)) continue;
            if      (k == "id")       c.id = parseInt(v);
            else if (k == "x")        c.x = parseInt(v);
            else if (k == "y")        c.y = parseInt(v);
            else if (k == "width")    c.w = parseInt(v);
            else if (k == "height")   c.h = parseInt(v);
            else if (k == "xoffset")  c.xOffset = parseInt(v);
            else if (k == "yoffset")  c.yOffset = parseInt(v);
            else if (k == "xadvance") c.xAdvance = parseInt(v);
            else if (k == "page")     c.page = parseInt(v);
        }
        FontGlyph g{};
        g.codepoint = static_cast<std::uint32_t>(c.id);
        g.x         = static_cast<std::uint16_t>(c.x);
        g.y         = static_cast<std::uint16_t>(c.y);
        g.w         = static_cast<std::uint16_t>(c.w);
        g.h         = static_cast<std::uint16_t>(c.h);
        g.xOffset   = static_cast<std::int16_t>(c.xOffset);
        g.yOffset   = static_cast<std::int16_t>(c.yOffset);
        g.xAdvance  = static_cast<std::int16_t>(c.xAdvance);
        g.page      = static_cast<std::uint8_t>(c.page);
        out.glyphs.push_back(g);
    } else if (tag == "kerning") {
        std::int64_t first = 0, second = 0, amount = 0;
        for (;;) {
            const auto tok = nextToken(rest);
            if (tok.empty()) break;
            std::string_view k, v;
            if (!splitKV(tok, k, v)) continue;
            if (k == "first")  first  = parseInt(v);
            else if (k == "second") second = parseInt(v);
            else if (k == "amount") amount = parseInt(v);
        }
        FontKerning kp;
        kp.first  = static_cast<std::uint32_t>(first);
        kp.second = static_cast<std::uint32_t>(second);
        kp.amount = static_cast<std::int16_t>(amount);
        out.kernings.push_back(kp);
    }
}

AssetResult<FontAtlas> parseBmfontText(std::span<const std::byte> bytes,
                                       std::string_view sourcePath,
                                       std::string_view assetDir) {
    FontAtlas out;
    out.sourcePath = std::string(sourcePath);

    std::vector<std::string> pageFiles;

    std::string_view src{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    std::size_t pos = 0;
    while (pos < src.size()) {
        std::size_t eol = pos;
        while (eol < src.size() && !isLineBreak(src[eol])) ++eol;
        std::string_view line = src.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        parseBmfontTextLine(line, out, pageFiles);
        pos = eol < src.size() ? eol + 1 : src.size();
    }

    // Sort kernings for binary search.
    std::sort(out.kernings.begin(), out.kernings.end(),
              [](const FontKerning& a, const FontKerning& b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second < b.second;
              });

    // Load pages.
    if (!assetDir.empty()) {
        for (const auto& f : pageFiles) {
            if (f.empty()) {
                out.pages.push_back(TextureData{});
                continue;
            }
            auto p = (std::filesystem::path(assetDir) / f).string();
            auto pr = loadPng(p);
            if (!pr.ok()) {
                return AssetResult<FontAtlas>::failure(pr.code,
                    "BMFont page failed: " + std::move(pr.message));
            }
            out.pages.push_back(std::move(pr.value));
        }
    }

    return AssetResult<FontAtlas>::success(std::move(out));
}

// Binary format -----------------------------------------------------------

template <class T>
T readLE(const std::byte* p) noexcept {
    T v{};
    std::memcpy(&v, p, sizeof(T));
    return v;
}

AssetResult<FontAtlas> parseBmfontBinary(std::span<const std::byte> bytes,
                                         std::string_view sourcePath,
                                         std::string_view assetDir) {
    FontAtlas out;
    out.sourcePath = std::string(sourcePath);
    std::vector<std::string> pageFiles;

    if (bytes.size() < 4) {
        return AssetResult<FontAtlas>::failure(
            ErrorCode::Truncated, "BMFont binary too short");
    }
    if (static_cast<char>(bytes[0]) != 'B' ||
        static_cast<char>(bytes[1]) != 'M' ||
        static_cast<char>(bytes[2]) != 'F') {
        return AssetResult<FontAtlas>::failure(
            ErrorCode::BadMagic, "BMFont binary signature mismatch");
    }
    const auto version = static_cast<std::uint8_t>(bytes[3]);
    if (version != 3) {
        return AssetResult<FontAtlas>::failure(
            ErrorCode::UnsupportedVersion, "BMFont binary version != 3");
    }

    std::size_t pos = 4;
    while (pos + 5 <= bytes.size()) {
        const auto block = static_cast<std::uint8_t>(bytes[pos]);
        const auto size  = readLE<std::uint32_t>(bytes.data() + pos + 1);
        const std::size_t body = pos + 5;
        if (body + size > bytes.size()) {
            return AssetResult<FontAtlas>::failure(
                ErrorCode::Truncated, "BMFont block body truncated");
        }
        const std::byte* d = bytes.data() + body;
        switch (block) {
            case 1: { // info
                if (size >= 15) {
                    out.fontSize = static_cast<std::uint16_t>(readLE<std::int16_t>(d));
                    const std::size_t nameStart = 14;
                    if (size > nameStart) {
                        out.fontName.assign(reinterpret_cast<const char*>(d + nameStart),
                                            size - nameStart - 1);
                    }
                }
                break;
            }
            case 2: { // common
                if (size >= 15) {
                    out.lineHeight = readLE<std::uint16_t>(d + 0);
                    out.base       = readLE<std::uint16_t>(d + 2);
                }
                break;
            }
            case 3: { // pages: N null-terminated names of equal length.
                std::size_t off = 0;
                while (off < size) {
                    const char* s = reinterpret_cast<const char*>(d + off);
                    const std::size_t n = std::strlen(s);
                    pageFiles.emplace_back(s, n);
                    off += n + 1;
                    if (n == 0) break;
                }
                break;
            }
            case 4: { // chars: 20 bytes each.
                const std::size_t count = size / 20;
                for (std::size_t i = 0; i < count; ++i) {
                    const std::byte* c = d + i * 20;
                    FontGlyph g{};
                    g.codepoint = readLE<std::uint32_t>(c + 0);
                    g.x         = readLE<std::uint16_t>(c + 4);
                    g.y         = readLE<std::uint16_t>(c + 6);
                    g.w         = readLE<std::uint16_t>(c + 8);
                    g.h         = readLE<std::uint16_t>(c + 10);
                    g.xOffset   = readLE<std::int16_t>(c + 12);
                    g.yOffset   = readLE<std::int16_t>(c + 14);
                    g.xAdvance  = readLE<std::int16_t>(c + 16);
                    g.page      = static_cast<std::uint8_t>(c[18]);
                    out.glyphs.push_back(g);
                }
                break;
            }
            case 5: { // kernings: 10 bytes each.
                const std::size_t count = size / 10;
                for (std::size_t i = 0; i < count; ++i) {
                    const std::byte* c = d + i * 10;
                    FontKerning k;
                    k.first  = readLE<std::uint32_t>(c + 0);
                    k.second = readLE<std::uint32_t>(c + 4);
                    k.amount = readLE<std::int16_t>(c + 8);
                    out.kernings.push_back(k);
                }
                break;
            }
            default: break;
        }
        pos = body + size;
    }

    std::sort(out.kernings.begin(), out.kernings.end(),
              [](const FontKerning& a, const FontKerning& b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second < b.second;
              });

    if (!assetDir.empty()) {
        for (const auto& f : pageFiles) {
            if (f.empty()) {
                out.pages.push_back(TextureData{});
                continue;
            }
            auto p = (std::filesystem::path(assetDir) / f).string();
            auto pr = loadPng(p);
            if (!pr.ok()) {
                return AssetResult<FontAtlas>::failure(pr.code,
                    "BMFont page failed: " + std::move(pr.message));
            }
            out.pages.push_back(std::move(pr.value));
        }
    }

    return AssetResult<FontAtlas>::success(std::move(out));
}

} // namespace

AssetResult<FontAtlas> parseBmfont(std::span<const std::byte> bytes,
                                   std::string_view sourcePath,
                                   std::string_view assetDir) {
    if (bytes.size() >= 4 &&
        static_cast<char>(bytes[0]) == 'B' &&
        static_cast<char>(bytes[1]) == 'M' &&
        static_cast<char>(bytes[2]) == 'F') {
        return parseBmfontBinary(bytes, sourcePath, assetDir);
    }
    return parseBmfontText(bytes, sourcePath, assetDir);
}

AssetResult<FontAtlas> loadBmfont(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<FontAtlas>::failure(bytes.code,
                                               std::move(bytes.message));
    }
    const auto parent = std::filesystem::path(path).parent_path().string();
    return parseBmfont(bytes.value, path, parent);
}

} // namespace threadmaxx::assets
