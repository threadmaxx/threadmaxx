#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "../types.hpp"

namespace threadmaxx::assets::detail {

AssetResult<std::vector<std::byte>> readFile(std::string_view path);

// Writes into a caller-owned vector to keep an alloc out of the hot
// path when the caller has a pre-warmed buffer. Returns Ok on success;
// `out` is cleared on failure.
ErrorCode readFileInto(std::string_view path, std::vector<std::byte>& out);

} // namespace threadmaxx::assets::detail
