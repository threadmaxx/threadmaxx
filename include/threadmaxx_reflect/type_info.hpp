#pragma once

/// @file type_info.hpp
/// @brief Runtime POD descriptors. `FieldInfo` mirrors a compile-time
/// `FieldDescriptor`; `TypeInfo` is the root the registry returns by
/// pointer. Storage lives in the registry — the `string_view` /
/// `std::span` members reference registry-owned memory whose lifetime
/// matches the registry's.

#include <cstdint>
#include <span>
#include <string_view>
#include <typeindex>

namespace threadmaxx::reflect {

/// @brief Per-field attribute (R5 populates these; R3 ships the POD).
struct AttributeInfo {
    std::string_view name{};
    std::string_view payload{};
};

/// @brief Runtime description of a single field in a registered type.
struct FieldInfo {
    std::string_view  name{};
    std::string_view  typeName{};
    std::type_index   typeIndex{typeid(void)};
    std::uint32_t     offset{0};
    std::uint32_t     sizeBytes{0};
    std::uint32_t     alignBytes{0};
    std::span<const AttributeInfo> attributes{};

    /// @brief Typed get — `T` must match the field's value type.
    template <typename T>
    const T* get(const void* obj) const noexcept {
        if (typeIndex != std::type_index(typeid(T))) return nullptr;
        return reinterpret_cast<const T*>(
            reinterpret_cast<const std::byte*>(obj) + offset);
    }

    /// @brief Typed set — `T` must match the field's value type.
    template <typename T>
    bool set(void* obj, const T& value) const noexcept {
        if (typeIndex != std::type_index(typeid(T))) return false;
        *reinterpret_cast<T*>(
            reinterpret_cast<std::byte*>(obj) + offset) = value;
        return true;
    }
};

/// @brief Runtime description of a registered type.
struct TypeInfo {
    std::string_view name{};
    std::type_index  typeIndex{typeid(void)};
    std::uint32_t    sizeBytes{0};
    std::uint32_t    alignBytes{0};
    std::span<const FieldInfo> fields{};

    /// @brief Find a field by name; nullptr if unknown.
    const FieldInfo* findField(std::string_view fieldName) const noexcept {
        for (const auto& f : fields) {
            if (f.name == fieldName) return &f;
        }
        return nullptr;
    }
};

} // namespace threadmaxx::reflect
