#pragma once

/// @file types.hpp
/// @brief Shared ID / result types used across threadmaxx_reflect.

#include <cstdint>
#include <string>

namespace threadmaxx::reflect {

/// @brief Stable per-process type identifier (incremented by registration).
struct TypeId {
    std::uint32_t value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(TypeId rhs) const noexcept { return value == rhs.value; }
    constexpr bool operator!=(TypeId rhs) const noexcept { return value != rhs.value; }
};

/// @brief Field index within a `TypeInfo::fields` span.
struct FieldId {
    std::uint16_t value{0xFFFFu};
    constexpr bool valid() const noexcept { return value != 0xFFFFu; }
    constexpr bool operator==(FieldId rhs) const noexcept { return value == rhs.value; }
};

enum class ErrorCode : std::uint8_t {
    Ok = 0,
    UnknownType,
    UnknownField,
    OutOfBounds,
    TypeMismatch,
    ParseError,
    NotAggregate,
    Unsupported,
};

/// @brief Result holder. `.ok()` and `.value` for success; `.code` /
/// `.message` for failure. Same shape as `threadmaxx_assets::AssetResult`.
template <typename T>
struct ReflectResult {
    T            value{};
    ErrorCode    code{ErrorCode::Ok};
    std::string  message{};

    bool ok() const noexcept { return code == ErrorCode::Ok; }

    static ReflectResult success(T v) { return ReflectResult{std::move(v), ErrorCode::Ok, {}}; }
    static ReflectResult failure(ErrorCode c, std::string msg = {}) {
        return ReflectResult{T{}, c, std::move(msg)};
    }
};

/// @brief Specialization for `void` results — carries only the code/message.
template <>
struct ReflectResult<void> {
    ErrorCode    code{ErrorCode::Ok};
    std::string  message{};

    bool ok() const noexcept { return code == ErrorCode::Ok; }

    static ReflectResult success() { return ReflectResult{ErrorCode::Ok, {}}; }
    static ReflectResult failure(ErrorCode c, std::string msg = {}) {
        return ReflectResult{c, std::move(msg)};
    }
};

} // namespace threadmaxx::reflect
