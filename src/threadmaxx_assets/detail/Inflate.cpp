#include "threadmaxx_assets/detail/inflate.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace threadmaxx::assets::detail {

namespace {

struct BitReader {
    const std::byte* p;
    const std::byte* end;
    std::uint32_t    buf{0};
    std::uint32_t    bits{0};
    bool             error{false};

    bool ensure(std::uint32_t n) noexcept {
        while (bits < n) {
            if (p >= end) { error = true; return false; }
            buf |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(*p++)) << bits;
            bits += 8;
        }
        return true;
    }

    std::uint32_t read(std::uint32_t n) noexcept {
        if (!ensure(n)) return 0;
        const std::uint32_t mask = (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
        const std::uint32_t v = buf & mask;
        buf >>= n;
        bits -= n;
        return v;
    }

    void byteAlign() noexcept {
        const std::uint32_t drop = bits & 7u;
        buf >>= drop;
        bits -= drop;
    }
};

// Huffman tree, canonical-code decode using the Deutsch trick.
struct Huff {
    std::array<std::uint16_t, 16> counts{};
    std::array<std::uint16_t, 288> symbols{};
};

bool buildHuff(Huff& h, const std::uint8_t* lengths, std::size_t n) noexcept {
    h.counts.fill(0);
    for (std::size_t i = 0; i < n; ++i) {
        const auto L = lengths[i];
        if (L > 15) return false;
        ++h.counts[L];
    }
    h.counts[0] = 0;

    std::array<std::uint16_t, 16> offsets{};
    std::uint16_t sum = 0;
    for (std::size_t L = 1; L <= 15; ++L) {
        offsets[L] = sum;
        sum = static_cast<std::uint16_t>(sum + h.counts[L]);
    }

    for (std::uint16_t s = 0; s < n; ++s) {
        const auto L = lengths[s];
        if (L == 0) continue;
        h.symbols[offsets[L]++] = s;
    }
    return true;
}

std::uint16_t decode(BitReader& br, const Huff& h) noexcept {
    int code = 0;
    int first = 0;
    int index = 0;
    for (std::size_t L = 1; L <= 15; ++L) {
        if (!br.ensure(1)) return 0;
        code |= static_cast<int>(br.read(1));
        const int count = static_cast<int>(h.counts[L]);
        if (code - count < first) {
            return h.symbols[static_cast<std::size_t>(index + (code - first))];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    br.error = true;
    return 0;
}

const std::uint16_t kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
const std::uint8_t kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
const std::uint16_t kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
    6145, 8193, 12289, 16385, 24577
};
const std::uint8_t kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
    8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

void buildFixed(Huff& litlen, Huff& dist) noexcept {
    std::uint8_t L[288];
    for (int i =   0; i < 144; ++i) L[i] = 8;
    for (int i = 144; i < 256; ++i) L[i] = 9;
    for (int i = 256; i < 280; ++i) L[i] = 7;
    for (int i = 280; i < 288; ++i) L[i] = 8;
    buildHuff(litlen, L, 288);
    std::uint8_t D[30];
    for (int i = 0; i < 30; ++i) D[i] = 5;
    buildHuff(dist, D, 30);
}

bool inflateBlock(BitReader& br, std::vector<std::byte>& out) noexcept {
    if (!br.ensure(3)) return false;
    const std::uint32_t bfinal = br.read(1);
    const std::uint32_t btype  = br.read(2);

    if (btype == 0) {
        br.byteAlign();
        if (!br.ensure(32)) return false;
        const std::uint16_t LEN  = static_cast<std::uint16_t>(br.read(16));
        const std::uint16_t NLEN = static_cast<std::uint16_t>(br.read(16));
        if (static_cast<std::uint16_t>(~LEN) != NLEN) return false;
        if (br.p + LEN > br.end) return false;
        const std::size_t pos = out.size();
        out.resize(pos + LEN);
        std::memcpy(out.data() + pos, br.p, LEN);
        br.p += LEN;
        return bfinal == 0;
    }

    Huff litlen, dist;
    if (btype == 1) {
        buildFixed(litlen, dist);
    } else if (btype == 2) {
        if (!br.ensure(14)) return false;
        const std::uint32_t HLIT  = br.read(5) + 257;
        const std::uint32_t HDIST = br.read(5) + 1;
        const std::uint32_t HCLEN = br.read(4) + 4;
        const std::uint8_t codeOrder[19] = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
        };
        std::uint8_t clens[19] = {};
        for (std::uint32_t i = 0; i < HCLEN; ++i) {
            if (!br.ensure(3)) return false;
            clens[codeOrder[i]] = static_cast<std::uint8_t>(br.read(3));
        }
        Huff codeTree;
        if (!buildHuff(codeTree, clens, 19)) return false;

        std::uint8_t lens[316] = {};
        const std::uint32_t total = HLIT + HDIST;
        std::uint32_t i = 0;
        while (i < total) {
            const auto sym = decode(br, codeTree);
            if (br.error) return false;
            if (sym < 16) {
                lens[i++] = static_cast<std::uint8_t>(sym);
            } else if (sym == 16) {
                if (i == 0) return false;
                if (!br.ensure(2)) return false;
                const auto rep = br.read(2) + 3;
                const auto prev = lens[i - 1];
                for (std::uint32_t k = 0; k < rep && i < total; ++k) lens[i++] = prev;
            } else if (sym == 17) {
                if (!br.ensure(3)) return false;
                const auto rep = br.read(3) + 3;
                for (std::uint32_t k = 0; k < rep && i < total; ++k) lens[i++] = 0;
            } else if (sym == 18) {
                if (!br.ensure(7)) return false;
                const auto rep = br.read(7) + 11;
                for (std::uint32_t k = 0; k < rep && i < total; ++k) lens[i++] = 0;
            } else {
                return false;
            }
        }
        if (!buildHuff(litlen, lens, HLIT)) return false;
        if (!buildHuff(dist, lens + HLIT, HDIST)) return false;
    } else {
        return false;
    }

    for (;;) {
        const auto sym = decode(br, litlen);
        if (br.error) return false;
        if (sym < 256) {
            out.push_back(static_cast<std::byte>(sym));
        } else if (sym == 256) {
            break;
        } else {
            const std::uint32_t lenIdx = sym - 257u;
            if (lenIdx >= 29) return false;
            if (!br.ensure(kLenExtra[lenIdx])) return false;
            const std::uint32_t L = kLenBase[lenIdx] +
                br.read(kLenExtra[lenIdx]);
            const auto dsym = decode(br, dist);
            if (br.error || dsym >= 30) return false;
            if (!br.ensure(kDistExtra[dsym])) return false;
            const std::uint32_t D = kDistBase[dsym] +
                br.read(kDistExtra[dsym]);
            if (D == 0 || D > out.size()) return false;
            const std::size_t srcPos = out.size() - D;
            out.reserve(out.size() + L);
            for (std::uint32_t k = 0; k < L; ++k) {
                out.push_back(out[srcPos + k]);
            }
        }
    }
    return bfinal == 0;
}

} // namespace

ErrorCode inflate(std::span<const std::byte> in,
                  std::vector<std::byte>& out,
                  bool wrapped) {
    out.clear();
    const std::byte* p = in.data();
    const std::byte* end = in.data() + in.size();

    if (wrapped) {
        if (in.size() < 6) return ErrorCode::Truncated;
        const auto cmf = static_cast<std::uint8_t>(*p++);
        const auto flg = static_cast<std::uint8_t>(*p++);
        if ((cmf & 0x0Fu) != 8) {
            return ErrorCode::UnsupportedFormat; // not deflate
        }
        if ((static_cast<std::uint32_t>(cmf) * 256u + flg) % 31u != 0) {
            return ErrorCode::ParseError; // bad zlib check
        }
        if ((flg & 0x20u) != 0) {
            return ErrorCode::UnsupportedFormat; // FDICT not supported
        }
        // ADLER32 trailing 4 bytes; we don't verify.
        end -= 4;
        if (end < p) return ErrorCode::Truncated;
    }

    BitReader br{p, end, 0, 0, false};
    bool moreBlocks = true;
    while (moreBlocks) {
        moreBlocks = inflateBlock(br, out);
        if (br.error) return ErrorCode::ParseError;
    }
    return ErrorCode::Ok;
}

} // namespace threadmaxx::assets::detail
