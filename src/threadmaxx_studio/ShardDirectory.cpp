/// @file ShardDirectory.cpp
/// @brief ST34 — ShardDirectory implementation.

#include <threadmaxx_studio/shard_directory.hpp>

#include <utility>

namespace threadmaxx::studio {

std::size_t ShardDirectory::addShard(ShardInfo info) {
    const auto index = shards_.size();
    shards_.push_back(std::move(info));
    return index;
}

bool ShardDirectory::select(std::size_t index) noexcept {
    if (index >= shards_.size()) return false;
    selected_ = index;
    return true;
}

bool ShardDirectory::selectByName(std::string_view name) noexcept {
    for (std::size_t i = 0; i < shards_.size(); ++i) {
        if (shards_[i].name == name) {
            selected_ = i;
            return true;
        }
    }
    return false;
}

const ShardInfo* ShardDirectory::selected() const noexcept {
    if (!selected_.has_value()) return nullptr;
    if (*selected_ >= shards_.size()) return nullptr;
    return &shards_[*selected_];
}

void ShardDirectory::markAlive(std::size_t index, bool alive) noexcept {
    if (index >= shards_.size()) return;
    shards_[index].alive = alive;
}

std::size_t ShardDirectory::aliveCount() const noexcept {
    std::size_t n = 0;
    for (const auto& s : shards_) if (s.alive) ++n;
    return n;
}

} // namespace threadmaxx::studio
