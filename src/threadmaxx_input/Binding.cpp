#include "threadmaxx_input/binding.hpp"

#include <cstring>

namespace threadmaxx::input {

void BindingSet::bind(ActionId id, Binding b) {
    auto it = index_.find(id);
    if (it == index_.end()) {
        const auto pos = entries_.size();
        entries_.push_back(Entry{id, {}});
        index_.emplace(id, pos);
        entries_.back().bindings.push_back(b);
    } else {
        entries_[it->second].bindings.push_back(b);
    }
}

void BindingSet::bind(std::string_view name, Binding b) {
    bind(actionId(name), b);
}

void BindingSet::clear(ActionId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;
    const std::size_t removed = it->second;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(removed));
    index_.erase(it);
    // Repair the index for entries shifted left.
    for (auto& kv : index_) {
        if (kv.second > removed) --kv.second;
    }
}

void BindingSet::clearAll() noexcept {
    entries_.clear();
    index_.clear();
}

std::span<const Binding> BindingSet::bindingsFor(ActionId id) const noexcept {
    auto it = index_.find(id);
    if (it == index_.end()) return {};
    const auto& bs = entries_[it->second].bindings;
    return std::span<const Binding>(bs.data(), bs.size());
}

bool BindingSet::contains(ActionId id) const noexcept {
    return index_.find(id) != index_.end();
}

std::vector<BindingSet::EntryView> BindingSet::entries() const {
    std::vector<EntryView> v;
    v.reserve(entries_.size());
    for (const auto& e : entries_) {
        v.push_back({e.id, std::span<const Binding>(e.bindings.data(), e.bindings.size())});
    }
    return v;
}

namespace {

template <typename T>
void appendBytes(std::vector<std::byte>& out, const T& value) {
    const auto* src = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), src, src + sizeof(T));
}

template <typename T>
bool readBytes(std::span<const std::byte> bytes, std::size_t& cursor, T& out) {
    if (cursor + sizeof(T) > bytes.size()) return false;
    std::memcpy(&out, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

}  // namespace

std::vector<std::byte> BindingSet::serialize() const {
    std::vector<std::byte> out;
    out.reserve(16 + entries_.size() * 32);

    appendBytes(out, kSerializeMagic);
    appendBytes(out, kSerializeVersion);
    const auto actionCount = static_cast<std::uint64_t>(entries_.size());
    appendBytes(out, actionCount);

    for (const auto& entry : entries_) {
        appendBytes(out, entry.id);
        const auto count = static_cast<std::uint32_t>(entry.bindings.size());
        appendBytes(out, count);
        for (const auto& b : entry.bindings) {
            appendBytes(out, b);
        }
    }
    return out;
}

bool BindingSet::deserialize(std::span<const std::byte> bytes) {
    std::size_t cursor = 0;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t actionCount = 0;
    if (!readBytes(bytes, cursor, magic)) return false;
    if (magic != kSerializeMagic) return false;
    if (!readBytes(bytes, cursor, version)) return false;
    if (version != kSerializeVersion) return false;
    if (!readBytes(bytes, cursor, actionCount)) return false;

    clearAll();
    entries_.reserve(actionCount);
    for (std::uint64_t i = 0; i < actionCount; ++i) {
        ActionId id = 0;
        std::uint32_t count = 0;
        if (!readBytes(bytes, cursor, id)) return false;
        if (!readBytes(bytes, cursor, count)) return false;
        Entry entry{id, {}};
        entry.bindings.reserve(count);
        for (std::uint32_t j = 0; j < count; ++j) {
            Binding b{};
            if (!readBytes(bytes, cursor, b)) return false;
            entry.bindings.push_back(b);
        }
        const auto pos = entries_.size();
        entries_.push_back(std::move(entry));
        index_.emplace(id, pos);
    }
    return true;
}

}  // namespace threadmaxx::input
