#pragma once

/// @file macro.hpp
/// @brief One-line type registration macro.
///
/// Usage (in the same TU as the type definition):
///
/// ```cpp
/// struct Health { int current; int max; float regen; };
/// THREADMAXX_REFLECT(Health, current, max, regen);
/// ```
///
/// The macro emits an ADL-discoverable hook
/// `_threadmaxx_reflect_fields_v1(T*) -> FieldList<…>` that
/// `for_each_field`, `TypeRegistry::registerType<T>`, and friends pick
/// up via concept-based dispatch. The type's namespace owns the hook
/// so the macro works equally well at file scope, inside `namespace
/// game { … }`, or inside a class with public visibility.

#include <cstddef>

#include "field_info.hpp"

#define THREADMAXX_REFLECT_FIELD_DESC_(T, FIELD) \
    ::threadmaxx::reflect::FieldDescriptor< \
        T, &T::FIELD, \
        ::threadmaxx::reflect::detail::FixedString{#FIELD}, \
        offsetof(T, FIELD) >

/// @brief Register `T` with reflect. `FIELDS` is a comma-separated
/// list of member names (1..16). Place at namespace scope in the same
/// namespace as the type definition (ADL lookup needs the hook visible
/// in the type's namespace).
///
/// `[[maybe_unused]]` silences clang's `-Wunneeded-internal-declaration`
/// when `T` lives in an anonymous namespace — the function is only ever
/// invoked from `decltype` contexts, never as a runtime call.
#define THREADMAXX_REFLECT(T, ...) \
    [[maybe_unused]] constexpr auto _threadmaxx_reflect_fields_v1(T*) noexcept { \
        return ::threadmaxx::reflect::FieldList< \
            T, \
            THREADMAXX_REFLECT_FE(THREADMAXX_REFLECT_FIELD_DESC_, T, __VA_ARGS__) \
        >{}; \
    }
