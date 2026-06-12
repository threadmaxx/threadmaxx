#pragma once

/// @file bitstream.hpp
/// @brief Bit-packed wire codec.
///
/// Header-only. Little-endian on the wire (the spec is fixed; the
/// reader/writer fully handle endianness internally so a wire stream
/// written on LE / BE / mixed targets reads back to the same logical
/// values). Used by every higher protocol layer.
///
/// Encoding rules:
/// - `writeBits(value, n)` packs the low n bits of `value`, LSB-first
///   into the current byte. Crossing byte boundaries shifts into the
///   next byte.
/// - `writeVarUInt(value)` writes 7 bits at a time with a continuation
///   bit (the same wire format used by protobuf / WebSockets etc).
/// - `writeBytes(span)` byte-aligns the cursor first, then copies raw
///   bytes — the writer's bit cursor is bumped to the next byte
///   boundary.
///
/// Reads mirror the writes. `exhausted()` returns true once the
/// reader has consumed every available bit; further reads return 0.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace threadmaxx::network {

class BitWriter {
public:
    BitWriter() = default;
    explicit BitWriter(std::size_t reserveBytes) {
        bytes_.reserve(reserveBytes);
    }

    /// @brief Write the low `bitCount` bits of `value` (0 <= bitCount <= 64).
    void writeBits(std::uint64_t value, std::uint32_t bitCount) {
        while (bitCount > 0) {
            if (bitPos_ == 0) bytes_.push_back(static_cast<std::byte>(0));
            const std::uint32_t roomInByte = 8u - bitPos_;
            const std::uint32_t take =
                bitCount < roomInByte ? bitCount : roomInByte;
            const std::uint64_t mask =
                (take == 64u) ? ~std::uint64_t{0}
                              : ((std::uint64_t{1} << take) - 1u);
            const std::uint8_t chunk =
                static_cast<std::uint8_t>(value & mask);
            bytes_.back() = static_cast<std::byte>(
                static_cast<std::uint8_t>(bytes_.back()) |
                static_cast<std::uint8_t>(chunk << bitPos_));
            value >>= take;
            bitCount -= take;
            bitPos_ += take;
            if (bitPos_ == 8u) bitPos_ = 0u;
        }
    }

    /// @brief Write a variable-length unsigned integer (7-bit per byte
    /// with a continuation bit). Byte-aligns the cursor first.
    void writeVarUInt(std::uint64_t value) {
        alignToByte();
        while (value >= 0x80u) {
            bytes_.push_back(static_cast<std::byte>(
                static_cast<std::uint8_t>(value) | 0x80u));
            value >>= 7;
        }
        bytes_.push_back(static_cast<std::byte>(value));
    }

    /// @brief Write raw bytes after byte-aligning.
    void writeBytes(std::span<const std::byte> data) {
        alignToByte();
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }

    /// @brief Skip to the next byte boundary if not already there.
    void alignToByte() {
        if (bitPos_ != 0) bitPos_ = 0;
    }

    /// @brief Finished payload as a byte view.
    std::span<const std::byte> finish() const noexcept {
        return {bytes_.data(), bytes_.size()};
    }

    /// @brief Number of bytes written so far.
    std::size_t sizeBytes() const noexcept { return bytes_.size(); }

    void clear() noexcept {
        bytes_.clear();
        bitPos_ = 0;
    }

private:
    std::vector<std::byte> bytes_{};
    std::uint32_t bitPos_{0};
};

class BitReader {
public:
    explicit BitReader(std::span<const std::byte> data) noexcept
        : data_(data) {}

    /// @brief Read `bitCount` bits (0..64). Returns 0 once exhausted.
    std::uint64_t readBits(std::uint32_t bitCount) noexcept {
        std::uint64_t out = 0;
        std::uint32_t outPos = 0;
        while (bitCount > 0) {
            if (bytePos_ >= data_.size()) {
                exhausted_ = true;
                break;
            }
            const std::uint32_t roomInByte = 8u - bitPos_;
            const std::uint32_t take =
                bitCount < roomInByte ? bitCount : roomInByte;
            const std::uint64_t mask =
                (take == 64u) ? ~std::uint64_t{0}
                              : ((std::uint64_t{1} << take) - 1u);
            const std::uint64_t chunk =
                (static_cast<std::uint64_t>(
                    static_cast<std::uint8_t>(data_[bytePos_])) >>
                 bitPos_) & mask;
            out |= chunk << outPos;
            outPos += take;
            bitCount -= take;
            bitPos_ += take;
            if (bitPos_ == 8u) {
                bitPos_ = 0u;
                ++bytePos_;
            }
        }
        return out;
    }

    /// @brief Read a varuint (7-bit groups + continuation). Returns
    /// 0 on exhaustion. Byte-aligns the cursor first.
    std::uint64_t readVarUInt() noexcept {
        alignToByte();
        std::uint64_t out = 0;
        std::uint32_t shift = 0;
        while (true) {
            if (bytePos_ >= data_.size()) {
                exhausted_ = true;
                return out;
            }
            const std::uint8_t b =
                static_cast<std::uint8_t>(data_[bytePos_++]);
            out |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0u) return out;
            shift += 7;
            if (shift >= 64u) return out; // protect against malformed stream
        }
    }

    /// @brief Byte-aligned read into `out`. Returns the number of bytes
    /// actually written (less than `out.size()` on exhaustion).
    std::size_t readBytes(std::span<std::byte> out) noexcept {
        alignToByte();
        const std::size_t avail = data_.size() - bytePos_;
        const std::size_t take = avail < out.size() ? avail : out.size();
        std::memcpy(out.data(), data_.data() + bytePos_, take);
        bytePos_ += take;
        if (take < out.size()) exhausted_ = true;
        return take;
    }

    /// @brief True once any read attempted to go past the end.
    bool exhausted() const noexcept { return exhausted_; }

    /// @brief Current cursor byte offset.
    std::size_t bytePos() const noexcept { return bytePos_; }

    void alignToByte() noexcept {
        if (bitPos_ != 0) {
            bitPos_ = 0;
            ++bytePos_;
        }
    }

private:
    std::span<const std::byte> data_;
    std::size_t bytePos_{0};
    std::uint32_t bitPos_{0};
    bool exhausted_{false};
};

} // namespace threadmaxx::network
