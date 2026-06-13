#pragma once

/// @file detail/binary_writer.hpp
/// @brief Trivial host-endian byte writer. Same caveat as engine's
/// WorldSnapshot: not portable across architectures.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace threadmaxx::migration::detail {

class BinaryWriter {
public:
    explicit BinaryWriter(std::vector<std::byte>& sink) noexcept
        : sink_(sink) {}

    template <class T>
    void write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto offset = sink_.size();
        sink_.resize(offset + sizeof(T));
        std::memcpy(sink_.data() + offset, &value, sizeof(T));
    }

    void writeBytes(std::span<const std::byte> bytes) {
        const auto offset = sink_.size();
        sink_.resize(offset + bytes.size());
        if (!bytes.empty()) {
            std::memcpy(sink_.data() + offset, bytes.data(), bytes.size());
        }
    }

    void writeString(std::string_view s) {
        write(static_cast<std::uint32_t>(s.size()));
        const auto offset = sink_.size();
        sink_.resize(offset + s.size());
        if (!s.empty()) {
            std::memcpy(sink_.data() + offset, s.data(), s.size());
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return sink_.size(); }

private:
    std::vector<std::byte>& sink_;
};

} // namespace threadmaxx::migration::detail
