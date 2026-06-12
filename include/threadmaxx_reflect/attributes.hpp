#pragma once

/// @file attributes.hpp
/// @brief Per-field attribute PODs and the runtime `addFieldAttribute`
/// registration API.
///
/// Attributes carry editor / tooling hints — value ranges, tooltips,
/// hidden flags, unit suffixes. Each attribute is a small POD with a
/// constexpr `kName` and a `formatPayload(char* buf, size_t cap)`
/// member that serializes to a short ASCII string the editor consumes
/// directly.
///
/// Attribute registration is a runtime call against `TypeRegistry`:
///
/// ```cpp
/// auto* h = reg.registerType<Health>("Health");
/// reg.addFieldAttribute(h, "current", Range{0.0, 100.0});
/// reg.addFieldAttribute(h, "current", Tooltip{"current hp"});
/// ```
///
/// The `THREADMAXX_REFLECT_WITH_ATTRS` macro form is intentionally
/// deferred to v1.x — runtime registration keeps the macro layer
/// simple and lets game-side code drive attributes from config / data
/// at startup rather than baked-in source.

#include <cstddef>
#include <cstdio>
#include <string_view>

namespace threadmaxx::reflect {

/// @brief Numeric range [min, max] — drives sliders / spin boxes.
struct Range {
    double min{0.0};
    double max{0.0};
    static constexpr std::string_view kName = "Range";
    std::size_t formatPayload(char* buf, std::size_t cap) const noexcept {
        const int n = std::snprintf(buf, cap, "%g,%g", min, max);
        return (n < 0) ? 0u : static_cast<std::size_t>(n);
    }
};

/// @brief Inspector tooltip text. Short ASCII string; no length cap
/// enforced beyond the registry's name-arena slab size.
struct Tooltip {
    std::string_view text{};
    static constexpr std::string_view kName = "Tooltip";
    std::size_t formatPayload(char* buf, std::size_t cap) const noexcept {
        const std::size_t n = (text.size() < cap) ? text.size() : cap;
        for (std::size_t i = 0; i < n; ++i) buf[i] = text[i];
        return n;
    }
};

/// @brief Hide the field from inspector panels (still settable
/// programmatically via Patch).
struct Hidden {
    static constexpr std::string_view kName = "Hidden";
    std::size_t formatPayload(char* /*buf*/, std::size_t /*cap*/) const noexcept {
        return 0;
    }
};

/// @brief Display-only unit suffix ("m/s", "kg", "hp").
struct Units {
    std::string_view suffix{};
    static constexpr std::string_view kName = "Units";
    std::size_t formatPayload(char* buf, std::size_t cap) const noexcept {
        const std::size_t n = (suffix.size() < cap) ? suffix.size() : cap;
        for (std::size_t i = 0; i < n; ++i) buf[i] = suffix[i];
        return n;
    }
};

/// @brief Mark a field as read-only in the inspector (still mutable
/// via Patch — this is a UI hint, not a memory guarantee).
struct ReadOnly {
    static constexpr std::string_view kName = "ReadOnly";
    std::size_t formatPayload(char* /*buf*/, std::size_t /*cap*/) const noexcept {
        return 0;
    }
};

/// @brief Step size for numeric editors (slider granularity).
struct Step {
    double value{1.0};
    static constexpr std::string_view kName = "Step";
    std::size_t formatPayload(char* buf, std::size_t cap) const noexcept {
        const int n = std::snprintf(buf, cap, "%g", value);
        return (n < 0) ? 0u : static_cast<std::size_t>(n);
    }
};

} // namespace threadmaxx::reflect
