#pragma once

/// @file name_arena.hpp
/// @brief Chained-slab string storage owned by the registry.
///
/// `TypeInfo::name`, `FieldInfo::name`, etc. are `std::string_view` —
/// the storage lives in the registry. Strings that come from the macro
/// (`FixedString` literals) are already statically allocated, so we
/// can reference them directly. Strings provided at runtime (e.g. a
/// user-supplied `name` parameter to `registerType<T>(...)`) get
/// copied into the arena.

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace threadmaxx::reflect::detail {

class NameArena {
public:
    NameArena() = default;
    NameArena(const NameArena&) = delete;
    NameArena& operator=(const NameArena&) = delete;
    NameArena(NameArena&&) noexcept = default;
    NameArena& operator=(NameArena&&) noexcept = default;

    /// @brief Copy `s` into the arena and return a view of the copy.
    std::string_view intern(std::string_view s) {
        if (s.empty()) return {};
        if (slabs_.empty() || slabs_.back().used + s.size() > slabs_.back().cap) {
            const std::size_t need = (s.size() > kDefaultSlabSize)
                                         ? s.size()
                                         : kDefaultSlabSize;
            slabs_.push_back(Slab{
                std::make_unique<char[]>(need),
                0,
                need,
            });
        }
        auto& slab = slabs_.back();
        char* dst = slab.data.get() + slab.used;
        for (std::size_t i = 0; i < s.size(); ++i) dst[i] = s[i];
        slab.used += s.size();
        return std::string_view(dst, s.size());
    }

private:
    static constexpr std::size_t kDefaultSlabSize = 4096;
    struct Slab {
        std::unique_ptr<char[]> data;
        std::size_t             used;
        std::size_t             cap;
    };
    std::vector<Slab> slabs_;
};

} // namespace threadmaxx::reflect::detail
