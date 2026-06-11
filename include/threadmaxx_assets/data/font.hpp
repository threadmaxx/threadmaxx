#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "texture.hpp"

namespace threadmaxx::assets {

struct FontGlyph {
    std::uint32_t codepoint{};
    std::uint16_t x{}, y{};
    std::uint16_t w{}, h{};
    std::int16_t  xOffset{}, yOffset{};
    std::int16_t  xAdvance{};
    std::uint8_t  page{};
};

struct FontKerning {
    std::uint32_t first{};
    std::uint32_t second{};
    std::int16_t  amount{};
};

struct FontAtlas {
    std::string              fontName;
    std::uint16_t            fontSize{};
    std::uint16_t            lineHeight{};
    std::uint16_t            base{};
    std::vector<TextureData> pages;
    std::vector<FontGlyph>   glyphs;
    std::vector<FontKerning> kernings;
    std::string              sourcePath;
};

} // namespace threadmaxx::assets
