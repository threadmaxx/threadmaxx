#pragma once

/// @file shard_directory.hpp
/// @brief ST34 — `ShardDirectory`: enumerate the available game shards
/// the studio can attach to, and remember which is currently selected.
///
/// Two population paths today, one wire-format extension later:
///   1. **Static list** — host code calls `addShard(info)` during
///      bring-up. Used by tests, single-process demos, deployments
///      that hardcode their topology.
///   2. **UDP discovery** (future) — a listener could populate the
///      directory by parsing beacon broadcasts. Out of scope here;
///      the data shape is intentionally network-agnostic.
///
/// Once a shard is selected the studio binds a `RemoteDataSource` to
/// that shard's `agentPeer`. Re-selecting tears the old
/// `RemoteDataSource` down + builds a fresh one — the directory
/// itself does not own the data source.

#include <threadmaxx_network/ids.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx::studio {

/// @brief Address + identity of one shard.
struct ShardInfo {
    /// @brief Human-readable name for the picker UI ("eu-west-01").
    std::string name;

    /// @brief Optional hostname / IPv4 literal for UDP discovery. The
    /// in-process loopback path ignores this.
    std::string host;

    /// @brief UDP port for transport setup. Zero for the in-process
    /// loopback path.
    std::uint16_t port{0};

    /// @brief Peer id of the running `StudioAgent` for in-process /
    /// loopback attach. Hosts that drive UDP can leave this default
    /// and look up the peer at connect-time.
    network::PeerId agentPeer{};

    /// @brief Last-known liveness. Maintained by the discovery layer
    /// (UDP heartbeat) or set manually for static lists.
    bool alive{false};
};

class ShardDirectory {
public:
    /// @brief Append a shard. Returns the inserted index.
    std::size_t addShard(ShardInfo info);

    /// @brief Read-only view of every registered shard.
    [[nodiscard]] std::span<const ShardInfo> shards() const noexcept {
        return std::span<const ShardInfo>{shards_.data(), shards_.size()};
    }

    [[nodiscard]] std::size_t shardCount() const noexcept {
        return shards_.size();
    }

    /// @brief Select by index. Returns true on success, false on
    /// out-of-range index (selection state unchanged).
    bool select(std::size_t index) noexcept;

    /// @brief Select by name (verbatim match). Returns true on a
    /// successful match, false when no shard carries that name
    /// (selection state unchanged).
    bool selectByName(std::string_view name) noexcept;

    /// @brief Drop the current selection. Subsequent
    /// `selectedIndex()` returns nullopt.
    void clearSelection() noexcept { selected_.reset(); }

    [[nodiscard]] std::optional<std::size_t> selectedIndex() const noexcept {
        return selected_;
    }

    /// @brief Pointer to the currently-selected shard, or `nullptr`
    /// if nothing is selected.
    [[nodiscard]] const ShardInfo* selected() const noexcept;

    /// @brief Update the liveness bit on a shard. No-op when @p index
    /// is out of range.
    void markAlive(std::size_t index, bool alive) noexcept;

    /// @brief Number of shards whose `alive` bit is set.
    [[nodiscard]] std::size_t aliveCount() const noexcept;

    /// @brief Drop every shard. Selection is cleared.
    void clear() noexcept {
        shards_.clear();
        selected_.reset();
    }

private:
    std::vector<ShardInfo>      shards_;
    std::optional<std::size_t>  selected_;
};

} // namespace threadmaxx::studio
