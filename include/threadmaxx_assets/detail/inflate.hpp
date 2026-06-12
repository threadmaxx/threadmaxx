#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "../types.hpp"

namespace threadmaxx::assets::detail {

// Minimal RFC1950 zlib + RFC1951 DEFLATE decoder. Used by the PNG
// loader. Supports the three block types: stored, fixed Huffman, and
// dynamic Huffman.
//
// `in` is the zlib stream (CMF/FLG header + DEFLATE payload + ADLER32).
// Set `wrapped=false` to consume a raw DEFLATE stream with no zlib
// wrapper (the rare bare-DEFLATE case; PNG always sets wrapped=true).
ErrorCode inflate(std::span<const std::byte> in,
                  std::vector<std::byte>& out,
                  bool wrapped = true);

} // namespace threadmaxx::assets::detail
