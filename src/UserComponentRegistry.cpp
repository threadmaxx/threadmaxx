/// @file UserComponentRegistry.cpp
/// Engine-side mapping of registered user component POD types to
/// @ref threadmaxx::UserComponentId tokens. See header for design notes.
#include "UserComponentRegistry.hpp"

namespace threadmaxx::internal {

UserComponentId UserComponentRegistry::reg(std::type_index type,
                                           std::uint32_t stride) noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& e : entries_) {
        if (e.type == type) {
            // Idempotent: re-registering returns the same token. If the
            // stride disagrees, the second registration loses — caller is
            // responsible for using one consistent T per typeid.
            return UserComponentId{e.bit, e.stride, e.type};
        }
    }
    if (nextBit_ >= 64) {
        // Out of user-bit space. Return an invalid id; addUserComponent
        // assertions will fire downstream.
        return UserComponentId{};
    }
    const std::uint32_t bit = nextBit_++;
    entries_.push_back(Entry{type, bit, stride});
    return UserComponentId{bit, stride, type};
}

UserComponentId UserComponentRegistry::find(std::type_index type) const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& e : entries_) {
        if (e.type == type) return UserComponentId{e.bit, e.stride, e.type};
    }
    return UserComponentId{};
}

std::uint32_t UserComponentRegistry::strideFor(std::uint32_t bit) const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& e : entries_) {
        if (e.bit == bit) return e.stride;
    }
    return 0;
}

std::size_t UserComponentRegistry::size() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return entries_.size();
}

} // namespace threadmaxx::internal
