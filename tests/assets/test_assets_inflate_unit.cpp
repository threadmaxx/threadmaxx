#include "Check.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "threadmaxx_assets/detail/inflate.hpp"

using namespace threadmaxx::assets;

int main() {
    // Stored block: BFINAL=1, BTYPE=00, LEN=5, "HELLO".
    // zlib header CMF=0x78, FLG=0x01.
    const std::uint8_t stored[] = {
        0x78, 0x01,
        0x01,                     // BFINAL=1, BTYPE=00
        0x05, 0x00, 0xFA, 0xFF,   // LEN=5 / NLEN=~5
        'H', 'E', 'L', 'L', 'O',
        0x00, 0x00, 0x00, 0x00,   // adler32 placeholder
    };
    std::vector<std::byte> outStored;
    auto err = detail::inflate(
        std::as_bytes(std::span<const std::uint8_t>(stored, sizeof(stored))),
        outStored, true);
    CHECK(err == ErrorCode::Ok);
    CHECK_EQ(outStored.size(), std::size_t{5});
    CHECK_EQ(static_cast<char>(outStored[0]), 'H');
    CHECK_EQ(static_cast<char>(outStored[4]), 'O');

    // Fixed-Huffman empty payload: zlib-compressed empty string.
    // Generated with: python -c "import zlib; print(' '.join(f'0x{b:02x}'
    // for b in zlib.compress(b'')))"
    // Output: 78 9c 03 00 00 00 00 01
    const std::uint8_t empty[] = {0x78, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::vector<std::byte> outEmpty;
    err = detail::inflate(
        std::as_bytes(std::span<const std::uint8_t>(empty, sizeof(empty))),
        outEmpty, true);
    CHECK(err == ErrorCode::Ok);
    CHECK_EQ(outEmpty.size(), std::size_t{0});

    // Truncated input.
    const std::uint8_t trunc[] = {0x78};
    std::vector<std::byte> outTrunc;
    err = detail::inflate(
        std::as_bytes(std::span<const std::uint8_t>(trunc, sizeof(trunc))),
        outTrunc, true);
    CHECK(err == ErrorCode::Truncated);

    EXIT_WITH_RESULT();
}
