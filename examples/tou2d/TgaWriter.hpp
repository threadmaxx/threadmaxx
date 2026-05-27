#pragma once

// Header-only 24-bit uncompressed TGA writer.
//
// Why TGA: the rest of the TOU asset pipeline already uses uncompressed
// TGA (`data/f_large.tga`, the level attribute maps, the GG theme
// sprites). Sticking with TGA means our intermediate dumps load with
// any image viewer the user already has wired up for TOU assets, and
// keeps the importer dependency-free (TGA's 18-byte header is the
// simplest mainstream image format we can produce without dragging in
// a PNG/JPEG encoder).
//
// We write the simplest profile that's universally supported:
//   image type 2  (uncompressed true-color)
//   pixel depth 24 (BGR, no alpha — TGA's native channel order)
//   origin     top-left (image descriptor 0x20)
//
// Pure header, no I/O dependency on the rest of the project — the
// caller passes an ostream or a path.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>

namespace tou2d::shp {

struct Rgb {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

inline bool writeTga24(const std::string& path,
                      std::uint16_t       width,
                      std::uint16_t       height,
                      std::span<const Rgb> pixels) {
    if (static_cast<std::size_t>(width) * height != pixels.size()) return false;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    std::uint8_t hdr[18] = {};
    hdr[2]  = 2;                       // image type = uncompressed truecolor
    hdr[12] = static_cast<std::uint8_t>(width  & 0xFF);
    hdr[13] = static_cast<std::uint8_t>(width  >> 8);
    hdr[14] = static_cast<std::uint8_t>(height & 0xFF);
    hdr[15] = static_cast<std::uint8_t>(height >> 8);
    hdr[16] = 24;                      // bits per pixel
    hdr[17] = 0x20;                    // origin top-left
    f.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));

    // TGA truecolor pixel order is BGR.
    for (const Rgb& p : pixels) {
        const std::uint8_t bgr[3] = { p.b, p.g, p.r };
        f.write(reinterpret_cast<const char*>(bgr), 3);
    }
    return static_cast<bool>(f);
}

} // namespace tou2d::shp
