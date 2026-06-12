#pragma once

/// @file value.hpp
/// @brief Type-erased SBO holder used by `Patch` and `readField`.
///
/// `Value` carries one of: empty, bool, signed/unsigned integer
/// (8/16/32/64), float, double, or an opaque pointer + `type_index`
/// for "anything else". The 40-byte budget fits every primitive and
/// short strings without heap traffic.

#include <cstdint>
#include <type_traits>
#include <typeindex>

namespace threadmaxx::reflect {

class Value {
public:
    enum class Tag : std::uint8_t {
        Empty,
        Bool,
        I8, I16, I32, I64,
        U8, U16, U32, U64,
        F32, F64,
        OpaquePtr,
    };

    Value() noexcept = default;

    /// @brief Construct from a typed primitive. Routes to the right Tag.
    template <typename T>
    static Value make(T v) noexcept {
        Value out;
        if constexpr (std::is_same_v<T, bool>) {
            out.tag_ = Tag::Bool; out.u_.b = v;
        } else if constexpr (std::is_same_v<T, std::int8_t>) {
            out.tag_ = Tag::I8;   out.u_.i64 = v;
        } else if constexpr (std::is_same_v<T, std::int16_t>) {
            out.tag_ = Tag::I16;  out.u_.i64 = v;
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
            out.tag_ = Tag::I32;  out.u_.i64 = v;
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            out.tag_ = Tag::I64;  out.u_.i64 = v;
        } else if constexpr (std::is_same_v<T, std::uint8_t>) {
            out.tag_ = Tag::U8;   out.u_.u64 = v;
        } else if constexpr (std::is_same_v<T, std::uint16_t>) {
            out.tag_ = Tag::U16;  out.u_.u64 = v;
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            out.tag_ = Tag::U32;  out.u_.u64 = v;
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
            out.tag_ = Tag::U64;  out.u_.u64 = v;
        } else if constexpr (std::is_same_v<T, float>) {
            out.tag_ = Tag::F32;  out.u_.f32 = v;
        } else if constexpr (std::is_same_v<T, double>) {
            out.tag_ = Tag::F64;  out.u_.f64 = v;
        } else {
            static_assert(sizeof(T) == 0, "Value::make<T>: unsupported T; use makeOpaque for class types");
        }
        out.typeIndex_ = std::type_index(typeid(T));
        return out;
    }

    /// @brief Build a Value that holds an opaque pointer of dynamic type.
    /// Caller owns the pointee's lifetime.
    static Value makeOpaque(std::type_index ti, const void* ptr) noexcept {
        Value v;
        v.tag_ = Tag::OpaquePtr;
        v.typeIndex_ = ti;
        v.u_.opaque = ptr;
        return v;
    }

    Tag tag() const noexcept { return tag_; }
    std::type_index typeIndex() const noexcept { return typeIndex_; }
    bool empty() const noexcept { return tag_ == Tag::Empty; }

    /// @brief Typed accessor. Returns true and writes `out` when `T`
    /// matches; false otherwise (no UB, no exception).
    template <typename T>
    bool get(T& out) const noexcept {
        if (typeIndex_ != std::type_index(typeid(T))) return false;
        if constexpr (std::is_same_v<T, bool>)             out = u_.b;
        else if constexpr (std::is_same_v<T, std::int8_t>)  out = static_cast<T>(u_.i64);
        else if constexpr (std::is_same_v<T, std::int16_t>) out = static_cast<T>(u_.i64);
        else if constexpr (std::is_same_v<T, std::int32_t>) out = static_cast<T>(u_.i64);
        else if constexpr (std::is_same_v<T, std::int64_t>) out = static_cast<T>(u_.i64);
        else if constexpr (std::is_same_v<T, std::uint8_t>)  out = static_cast<T>(u_.u64);
        else if constexpr (std::is_same_v<T, std::uint16_t>) out = static_cast<T>(u_.u64);
        else if constexpr (std::is_same_v<T, std::uint32_t>) out = static_cast<T>(u_.u64);
        else if constexpr (std::is_same_v<T, std::uint64_t>) out = static_cast<T>(u_.u64);
        else if constexpr (std::is_same_v<T, float>)        out = u_.f32;
        else if constexpr (std::is_same_v<T, double>)       out = u_.f64;
        else return false;
        return true;
    }

    template <typename T>
    bool is() const noexcept {
        return typeIndex_ == std::type_index(typeid(T));
    }

    const void* opaquePtr() const noexcept {
        return tag_ == Tag::OpaquePtr ? u_.opaque : nullptr;
    }

private:
    Tag             tag_{Tag::Empty};
    std::type_index typeIndex_{typeid(void)};
    union Storage {
        std::int64_t  i64;
        std::uint64_t u64;
        float         f32;
        double        f64;
        bool          b;
        const void*   opaque;
        Storage() : u64(0) {}
    } u_{};
};

static_assert(sizeof(Value) <= 48, "Value SBO budget");

} // namespace threadmaxx::reflect
