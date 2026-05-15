# Embed a SPIR-V binary into a C++ header as a constexpr std::uint32_t array.
# Inputs:
#   SPV_INPUT  — path to the .spv file
#   SPV_OUTPUT — path to the generated .hpp
#   SPV_SYMBOL — C++ symbol name to expose

file(READ ${SPV_INPUT} hex HEX)
string(LENGTH ${hex} hex_len)
math(EXPR word_count "${hex_len} / 8")

set(body "")
set(idx 0)
set(per_line 6)
while (idx LESS word_count)
    math(EXPR off "${idx} * 8")
    string(SUBSTRING ${hex} ${off} 8 word_hex)
    # SPIR-V is little-endian; reverse the byte order.
    string(SUBSTRING ${word_hex} 0 2 b0)
    string(SUBSTRING ${word_hex} 2 2 b1)
    string(SUBSTRING ${word_hex} 4 2 b2)
    string(SUBSTRING ${word_hex} 6 2 b3)
    set(word "0x${b3}${b2}${b1}${b0}u")
    math(EXPR mod "${idx} % ${per_line}")
    if (mod EQUAL 0)
        string(APPEND body "\n    ")
    endif()
    string(APPEND body "${word}, ")
    math(EXPR idx "${idx} + 1")
endwhile()

file(WRITE ${SPV_OUTPUT}
"// Auto-generated from ${SPV_INPUT}. Do not edit by hand.
#pragma once

#include <array>
#include <cstdint>

namespace threadmaxx_vk {

inline constexpr std::array<std::uint32_t, ${word_count}> ${SPV_SYMBOL} = {${body}
};

} // namespace threadmaxx_vk
")
