#pragma once

/// @file detail/binary_reader.hpp
/// @brief Trivial host-endian byte reader. Same caveat as engine's
/// WorldSnapshot: not portable across architectures.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace threadmaxx::migration::detail {

class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::byte> bytes) noexcept
        : bytes_(bytes), pos_(0), error_(false) {}

    template <class T>
    bool read(T& out) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (error_ || pos_ + sizeof(T) > bytes_.size()) {
            error_ = true;
            return false;
        }
        std::memcpy(&out, bytes_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    bool readBytes(std::vector<std::byte>& out, std::size_t count) {
        if (error_ || pos_ + count > bytes_.size()) {
            error_ = true;
            return false;
        }
        out.resize(count);
        if (count != 0) {
            std::memcpy(out.data(), bytes_.data() + pos_, count);
        }
        pos_ += count;
        return true;
    }

    bool readString(std::string& out) {
        std::uint32_t len{};
        if (!read(len)) return false;
        if (error_ || pos_ + len > bytes_.size()) {
            error_ = true;
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes_.data() + pos_), len);
        pos_ += len;
        return true;
    }

    [[nodiscard]] bool ok() const noexcept { return !error_; }
    [[nodiscard]] std::size_t bytesRead() const noexcept { return pos_; }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return error_ ? 0u : (bytes_.size() - pos_);
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t pos_;
    bool error_;
};

} // namespace threadmaxx::migration::detail
