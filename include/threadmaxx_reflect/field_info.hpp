#pragma once

/// @file field_info.hpp
/// @brief Compile-time field descriptors emitted by the
/// `THREADMAXX_REFLECT` macro.
///
/// `FieldDescriptor<T, MemberPtr, Name, Offset>` carries everything the
/// runtime `TypeInfo` (R3) needs to render a field: name, value type,
/// offset, and a typed accessor. `FieldList<T, Desc...>` is the
/// variadic container the ADL hook returns; `for_each(obj, fn)` walks
/// every descriptor and invokes `fn(name, value)`.

#include <cstddef>
#include <string_view>
#include <type_traits>

#include "detail/macro_impl.hpp"

namespace threadmaxx::reflect {

/// @brief Per-field compile-time descriptor.
///
/// `MemberPtr` is a pointer-to-member; `Name` is a `FixedString` literal
/// the macro generates from `#field`; `Offset` is `offsetof(T, field)`.
template <typename T, auto MemberPtr, detail::FixedString Name, std::size_t Offset>
struct FieldDescriptor {
    using ClassType = T;
    using ValueType =
        typename detail::member_type<std::remove_cv_t<decltype(MemberPtr)>>::value_type;

    static constexpr std::string_view name() noexcept { return Name.view(); }
    static constexpr std::size_t       offset() noexcept { return Offset; }
    static constexpr std::size_t       sizeBytes() noexcept { return sizeof(ValueType); }
    static constexpr std::size_t       alignBytes() noexcept { return alignof(ValueType); }

    template <typename U>
    static constexpr auto& get(U& obj) noexcept {
        return obj.*MemberPtr;
    }
};

/// @brief Variadic container for a type's `FieldDescriptor`s.
template <typename T, typename... Descs>
struct FieldList {
    using ClassType = T;
    static constexpr std::size_t size = sizeof...(Descs);

    /// @brief Walk every field, invoking `fn(name, value)`.
    template <typename Fn>
    static constexpr void for_each(T& obj, Fn&& fn) {
        (fn(Descs::name(), Descs::get(obj)), ...);
    }

    /// @brief const overload.
    template <typename Fn>
    static constexpr void for_each(const T& obj, Fn&& fn) {
        (fn(Descs::name(), Descs::get(obj)), ...);
    }
};

namespace detail {

/// @brief ADL probe — `true` when `_threadmaxx_reflect_fields_v1(T*)` is
/// discoverable for `T` (i.e. the user applied `THREADMAXX_REFLECT`).
template <typename T>
concept HasReflectHook = requires(T* p) {
    { _threadmaxx_reflect_fields_v1(p) };
};

} // namespace detail

} // namespace threadmaxx::reflect
