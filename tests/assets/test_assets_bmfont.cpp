#include "Check.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "threadmaxx_assets/loaders/bmfont.hpp"

using namespace threadmaxx::assets;

namespace {

constexpr std::string_view kFntText = R"(
info face="TestFont" size=16
common lineHeight=20 base=14 scaleW=128 scaleH=128 pages=1
page id=0 file="missing.png"
chars count=2
char id=65 x=0  y=0 width=8 height=12 xoffset=0 yoffset=2 xadvance=9  page=0
char id=66 x=8  y=0 width=8 height=12 xoffset=0 yoffset=2 xadvance=9  page=0
kernings count=1
kerning first=65 second=66 amount=-1
)";

void appendBytes(std::vector<std::byte>& dst, const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    dst.insert(dst.end(), b, b + n);
}

template <class T>
void appendLE(std::vector<std::byte>& dst, T v) {
    appendBytes(dst, &v, sizeof(T));
}

std::vector<std::byte> buildBinaryBmfont() {
    std::vector<std::byte> b;
    b.push_back(std::byte{'B'});
    b.push_back(std::byte{'M'});
    b.push_back(std::byte{'F'});
    b.push_back(std::byte{3});

    // info block (id=1)
    {
        std::vector<std::byte> body;
        body.resize(14, std::byte{0});
        std::int16_t size = 16;
        std::memcpy(body.data(), &size, 2);
        const char* name = "TestFont";
        appendBytes(body, name, std::strlen(name) + 1);

        b.push_back(std::byte{1});
        appendLE<std::uint32_t>(b, static_cast<std::uint32_t>(body.size()));
        appendBytes(b, body.data(), body.size());
    }
    // common block (id=2)
    {
        std::vector<std::byte> body(15, std::byte{0});
        std::uint16_t lh = 20, base = 14;
        std::memcpy(body.data() + 0, &lh, 2);
        std::memcpy(body.data() + 2, &base, 2);
        b.push_back(std::byte{2});
        appendLE<std::uint32_t>(b, static_cast<std::uint32_t>(body.size()));
        appendBytes(b, body.data(), body.size());
    }
    // pages block (id=3): one zero-byte name
    {
        const char* page0 = "";
        const std::size_t bodySize = std::strlen(page0) + 1;
        b.push_back(std::byte{3});
        appendLE<std::uint32_t>(b, static_cast<std::uint32_t>(bodySize));
        appendBytes(b, page0, bodySize);
    }
    // chars block (id=4): two glyphs at 20 bytes each
    {
        std::vector<std::byte> body;
        for (std::uint32_t id : {65u, 66u}) {
            std::vector<std::byte> rec(20, std::byte{0});
            std::memcpy(rec.data() + 0, &id, 4);
            std::uint16_t w = 8, h = 12;
            std::memcpy(rec.data() + 8,  &w, 2);
            std::memcpy(rec.data() + 10, &h, 2);
            std::int16_t xadv = 9;
            std::memcpy(rec.data() + 16, &xadv, 2);
            appendBytes(body, rec.data(), rec.size());
        }
        b.push_back(std::byte{4});
        appendLE<std::uint32_t>(b, static_cast<std::uint32_t>(body.size()));
        appendBytes(b, body.data(), body.size());
    }
    // kernings block (id=5): one entry
    {
        std::vector<std::byte> body(10, std::byte{0});
        std::uint32_t first = 65, second = 66;
        std::int16_t amount = -1;
        std::memcpy(body.data() + 0, &first,  4);
        std::memcpy(body.data() + 4, &second, 4);
        std::memcpy(body.data() + 8, &amount, 2);
        b.push_back(std::byte{5});
        appendLE<std::uint32_t>(b, static_cast<std::uint32_t>(body.size()));
        appendBytes(b, body.data(), body.size());
    }

    return b;
}

} // namespace

int main() {
    // Text variant.
    {
        auto bytes = std::as_bytes(std::span<const char>(kFntText.data(), kFntText.size()));
        auto r = parseBmfont(bytes, "test.fnt", /*assetDir=*/"");
        CHECK(r.ok());
        if (!r.ok()) {
            EXIT_WITH_RESULT();
        }
        const auto& a = r.value;
        CHECK_EQ(a.fontName, "TestFont");
        CHECK_EQ(a.fontSize,   std::uint16_t{16});
        CHECK_EQ(a.lineHeight, std::uint16_t{20});
        CHECK_EQ(a.base,       std::uint16_t{14});
        CHECK_EQ(a.glyphs.size(),   std::size_t{2});
        CHECK_EQ(a.kernings.size(), std::size_t{1});
        CHECK_EQ(a.glyphs[0].codepoint, std::uint32_t{65});
        CHECK_EQ(a.glyphs[1].codepoint, std::uint32_t{66});
        CHECK_EQ(a.kernings[0].amount,  std::int16_t{-1});
    }

    // Binary variant.
    {
        const auto bytes = buildBinaryBmfont();
        auto r = parseBmfont(std::span<const std::byte>(bytes), "bin.fnt", "");
        CHECK(r.ok());
        if (!r.ok()) {
            EXIT_WITH_RESULT();
        }
        const auto& a = r.value;
        CHECK_EQ(a.fontName, "TestFont");
        CHECK_EQ(a.fontSize,   std::uint16_t{16});
        CHECK_EQ(a.lineHeight, std::uint16_t{20});
        CHECK_EQ(a.glyphs.size(),   std::size_t{2});
        CHECK_EQ(a.kernings.size(), std::size_t{1});
        CHECK_EQ(a.glyphs[0].codepoint, std::uint32_t{65});
        CHECK_EQ(a.kernings[0].first,   std::uint32_t{65});
        CHECK_EQ(a.kernings[0].second,  std::uint32_t{66});
        CHECK_EQ(a.kernings[0].amount,  std::int16_t{-1});
    }

    // Binary-search lookup using std::lower_bound on the sorted kerning array.
    {
        auto bytes = std::as_bytes(std::span<const char>(kFntText.data(), kFntText.size()));
        auto r = parseBmfont(bytes, "", "");
        CHECK(r.ok());
        const auto& kerns = r.value.kernings;
        FontKerning probe{65, 66, 0};
        auto it = std::lower_bound(kerns.begin(), kerns.end(), probe,
            [](const FontKerning& a, const FontKerning& b) {
                if (a.first != b.first) return a.first < b.first;
                return a.second < b.second;
            });
        CHECK(it != kerns.end());
        CHECK_EQ(it->amount, std::int16_t{-1});
    }

    EXIT_WITH_RESULT();
}
