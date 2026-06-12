/// @file ReplicationRegistry.cpp

#include "threadmaxx_network/replication.hpp"

#include <string>

namespace threadmaxx::network {

namespace {

std::uint64_t fnv1a(std::string_view s, std::uint64_t seed) noexcept {
    std::uint64_t h = seed;
    for (char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

std::uint32_t ReplicationRegistry::registerComponent(std::string_view name,
                                                     std::uint32_t fieldCount) {
    auto it = byName_.find(std::string(name));
    if (it != byName_.end()) return it->second;
    const std::uint32_t id = static_cast<std::uint32_t>(entries_.size() + 1);
    entries_.push_back({std::string(name), fieldCount});
    byName_.emplace(entries_.back().name, id);
    cachedHash_ = 0;
    return id;
}

std::uint32_t ReplicationRegistry::codecId(std::string_view name) const noexcept {
    auto it = byName_.find(std::string(name));
    return it != byName_.end() ? it->second : 0u;
}

std::uint32_t ReplicationRegistry::fieldCount(std::uint32_t codecId) const noexcept {
    if (codecId == 0 || codecId > entries_.size()) return 0;
    return entries_[codecId - 1].fieldCount;
}

std::uint64_t ReplicationRegistry::schemaHash() const noexcept {
    if (cachedHash_ != 0) return cachedHash_;
    std::uint64_t h = 14695981039346656037ull;
    for (const auto& e : entries_) {
        h = fnv1a(e.name, h);
        h ^= e.fieldCount;
        h *= 1099511628211ull;
    }
    if (h == 0) h = 1; // never return 0; that's the "unknown" sentinel
    cachedHash_ = h;
    return h;
}

} // namespace threadmaxx::network
