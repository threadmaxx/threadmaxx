#pragma once

/// @file registry.hpp
/// @brief Thread-safe runtime type registry.
///
/// `TypeRegistry` owns `TypeInfo` storage in a stable-pointer container
/// (deque) so pointers handed out to subscribers never invalidate. A
/// process-wide default instance is the macro's auto-registration
/// target; tools / tests can spin up isolated registries.
///
/// Hot path: `find(typeName)` / `find(type_index)` take a `shared_mutex`
/// read lock — no allocations after warmup. Registration takes the
/// writer lock; idempotent on `typeid(T)`.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <vector>

#include "field_info.hpp"
#include "type_info.hpp"
#include "types.hpp"

namespace threadmaxx::reflect {

namespace detail {

/// @brief Pretty-name for a type. Strips namespaces / template clutter
/// for primitives; for everything else returns the raw demangled-ish
/// form via `typeid(T).name()` (registered as `"T"` placeholder until
/// R6's JsonVisitor extends this).
template <typename T>
constexpr std::string_view prettyTypeName() noexcept {
    if constexpr (std::is_same_v<T, bool>)            return "bool";
    else if constexpr (std::is_same_v<T, char>)       return "char";
    else if constexpr (std::is_same_v<T, std::int8_t>)   return "int8";
    else if constexpr (std::is_same_v<T, std::int16_t>)  return "int16";
    else if constexpr (std::is_same_v<T, std::int32_t>)  return "int32";
    else if constexpr (std::is_same_v<T, std::int64_t>)  return "int64";
    else if constexpr (std::is_same_v<T, std::uint8_t>)  return "uint8";
    else if constexpr (std::is_same_v<T, std::uint16_t>) return "uint16";
    else if constexpr (std::is_same_v<T, std::uint32_t>) return "uint32";
    else if constexpr (std::is_same_v<T, std::uint64_t>) return "uint64";
    else if constexpr (std::is_same_v<T, float>)      return "float";
    else if constexpr (std::is_same_v<T, double>)     return "double";
    else                                              return "T";
}

/// @brief Walk a `FieldList<T, Descs...>` to produce runtime `FieldInfo`s.
template <typename T, typename... Descs>
std::vector<FieldInfo> collectFieldInfos(FieldList<T, Descs...>*) {
    std::vector<FieldInfo> out;
    out.reserve(sizeof...(Descs));
    (out.push_back(FieldInfo{
        Descs::name(),
        detail::prettyTypeName<typename Descs::ValueType>(),
        std::type_index(typeid(typename Descs::ValueType)),
        static_cast<std::uint32_t>(Descs::offset()),
        static_cast<std::uint32_t>(Descs::sizeBytes()),
        static_cast<std::uint32_t>(Descs::alignBytes()),
        {}, // attributes — R5
    }), ...);
    return out;
}

} // namespace detail

class TypeRegistry {
public:
    TypeRegistry();
    ~TypeRegistry();
    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;
    TypeRegistry(TypeRegistry&&) = delete;
    TypeRegistry& operator=(TypeRegistry&&) = delete;

    /// @brief Register `T` using its `_threadmaxx_reflect_fields_v1`
    /// hook (emitted by `THREADMAXX_REFLECT`). Idempotent on
    /// `typeid(T)` — subsequent calls return the existing `TypeInfo*`.
    /// @param nameOverride Optional friendly name; defaults to
    /// `typeid(T).name()` mangled — usually you DO want to pass this.
    template <typename T>
    const TypeInfo* registerType(std::string_view nameOverride = {}) {
        static_assert(detail::HasReflectHook<T>,
                      "registerType<T>: T must be registered with THREADMAXX_REFLECT");
        using FL = decltype(_threadmaxx_reflect_fields_v1(static_cast<T*>(nullptr)));
        auto fields = detail::collectFieldInfos<T>(static_cast<FL*>(nullptr));
        return registerTypeImpl(
            std::type_index(typeid(T)),
            nameOverride,
            static_cast<std::uint32_t>(sizeof(T)),
            static_cast<std::uint32_t>(alignof(T)),
            std::move(fields));
    }

    /// @brief Lookup by name. nullptr if unregistered.
    const TypeInfo* find(std::string_view name) const noexcept;
    /// @brief Lookup by `type_index`. nullptr if unregistered.
    const TypeInfo* find(std::type_index ti) const noexcept;

    /// @brief Every registered TypeInfo*, in registration order.
    std::span<const TypeInfo* const> all() const noexcept;
    /// @brief Count of registered types.
    std::size_t size() const noexcept;

    /// @brief Process-wide default instance.
    static TypeRegistry& defaultInstance() noexcept;

private:
    const TypeInfo* registerTypeImpl(
        std::type_index ti,
        std::string_view nameOverride,
        std::uint32_t sizeBytes,
        std::uint32_t alignBytes,
        std::vector<FieldInfo> fields);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx::reflect
