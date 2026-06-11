#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace threadmaxx::assets {

using AssetId = std::uint32_t;

inline constexpr AssetId kInvalidAssetId = 0xFFFFFFFFu;

enum class AssetType : std::uint8_t {
    Unknown = 0,
    Mesh,
    Texture,
    Audio,
    Font,
    Bundle
};

enum class ErrorCode : std::uint16_t {
    Ok = 0,
    FileNotFound,
    IoError,
    BadMagic,
    UnsupportedVersion,
    UnsupportedFormat,
    Truncated,
    ParseError,
    OutOfMemory,
    HashMismatch
};

template <class T>
struct AssetResult {
    T          value{};
    ErrorCode  code{ErrorCode::Ok};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::Ok; }
    explicit operator bool() const noexcept { return ok(); }

    static AssetResult success(T v) {
        AssetResult r;
        r.value = std::move(v);
        r.code  = ErrorCode::Ok;
        return r;
    }
    static AssetResult failure(ErrorCode c, std::string msg) {
        AssetResult r;
        r.code    = c;
        r.message = std::move(msg);
        return r;
    }
};

} // namespace threadmaxx::assets
