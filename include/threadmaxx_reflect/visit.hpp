#pragma once

/// @file visit.hpp
/// @brief Ready-made visitors over reflected types.
///
/// `visit_fields(obj, visitor)` is the entry point. Visitors are
/// classes whose `operator()(string_view name, auto& value)` (macro
/// path) and/or `operator()(size_t index, auto& value)` (aggregate
/// path) handle each field. Visitors ship: `PrintVisitor`,
/// `HashVisitor`, `EqualsVisitor`. The JSON binder lives in
/// `binders/json.hpp` (and depends on `type_info.hpp` for the runtime
/// `to_json` path).

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "aggregate.hpp"

namespace threadmaxx::reflect {

/// @brief Alias for the dispatching for_each_field. Clarifies intent
/// at call sites that consume a visitor.
template <typename T, typename Visitor>
constexpr void visit_fields(T& obj, Visitor&& v) {
    for_each_field(obj, static_cast<Visitor&&>(v));
}

/// @brief Stringify a reflected object as `TypeName{field=value, …}`.
/// Caller owns the buffer; appends to it.
class PrintVisitor {
public:
    explicit PrintVisitor(std::string& out) : out_(out) {}

    template <typename V>
    void operator()(std::string_view name, V& value) {
        if (!first_) out_ += ", ";
        first_ = false;
        out_ += name;
        out_ += "=";
        appendValue(value);
    }

    template <typename V>
    void operator()(std::size_t index, V& value) {
        if (!first_) out_ += ", ";
        first_ = false;
        out_ += "field_";
        out_ += std::to_string(index);
        out_ += "=";
        appendValue(value);
    }

private:
    template <typename V>
    void appendValue(V& v) {
        using B = std::remove_cv_t<std::remove_reference_t<V>>;
        if constexpr (std::is_same_v<B, bool>) {
            out_ += (v ? "true" : "false");
        } else if constexpr (std::is_integral_v<B>) {
            out_ += std::to_string(static_cast<long long>(v));
        } else if constexpr (std::is_floating_point_v<B>) {
            out_ += std::to_string(static_cast<double>(v));
        } else {
            out_ += "?";
        }
    }

    std::string& out_;
    bool         first_{true};
};

/// @brief FNV-1a-64 fingerprint of every field's raw bytes, in
/// declaration order. Two equal-by-value objects produce the same
/// hash; equal hashes don't strictly imply equality (it's a fingerprint).
class HashVisitor {
public:
    template <typename V>
    void operator()(std::string_view /*name*/, V& value) { mixField(value); }

    template <typename V>
    void operator()(std::size_t /*index*/, V& value) { mixField(value); }

    std::uint64_t hash() const noexcept { return state_; }

private:
    template <typename V>
    void mixField(const V& v) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&v);
        for (std::size_t i = 0; i < sizeof(V); ++i) {
            state_ ^= bytes[i];
            state_ *= 0x100000001B3ull;
        }
    }

    std::uint64_t state_{0xCBF29CE484222325ull};
};

/// @brief Equality check field-by-field. The visitor only sees one of
/// the two objects at a time, so callers run it twice and compare the
/// resulting fingerprints. Convenience helper:
template <typename T>
bool fields_equal(const T& a, const T& b) {
    HashVisitor ha, hb;
    visit_fields(const_cast<T&>(a), ha);  // visitors don't mutate
    visit_fields(const_cast<T&>(b), hb);
    return ha.hash() == hb.hash();
}

} // namespace threadmaxx::reflect
