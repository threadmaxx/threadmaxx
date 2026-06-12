#pragma once

/// @file interest.hpp
/// @brief Area-of-interest (AOI) filters.
///
/// Per-client filter that the server consults before sending each
/// entity: only entities within the client's AOI are replicated.
/// Critical at MMORPG scale where the server holds tens of thousands
/// of entities but each client cares about a few hundred.
///
/// v1.0 supplies:
/// - distance-based AOI (radius around a focus position),
/// - per-entity explicit allow-list (always replicate this set),
/// - enter/exit deltas (the filter reports which entities just became
///   visible or invisible since the last query — game code uses these
///   to emit spawn/despawn replication entries).

#include "ids.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace threadmaxx::network {

/// @brief One position-anchored entity record. The replication layer
/// queries `InterestManager::buildVisibleSet` once per tick per
/// client; game code fills the `positions` array beforehand.
struct EntityPosition {
    NetEntityId id{};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct InterestConfig {
    /// @brief Radius (world units) around each client's focus position.
    /// `0.0f` disables distance filtering; only explicit-allow-list
    /// entries are replicated.
    float radius{100.0f};
};

/// @brief Per-client AOI state tracked across ticks.
struct ClientFocus {
    PeerId peer{};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    InterestConfig config{};
    /// @brief Entities that the client *always* sees (player party,
    /// quest objectives, scripted events). Bypasses distance filter.
    std::unordered_set<std::uint64_t> allowList{};
};

/// @brief Result of one `buildVisibleSet` call.
struct VisibilitySet {
    /// @brief All entity ids currently visible to the client.
    std::vector<NetEntityId> visible{};
    /// @brief Entities that just became visible since the previous
    /// query. Game code emits spawn replication for these.
    std::vector<NetEntityId> entered{};
    /// @brief Entities that just became invisible since the previous
    /// query. Game code emits despawn for these.
    std::vector<NetEntityId> exited{};
};

class InterestManager {
public:
    /// @brief Register / replace the focus for a client. Idempotent.
    void setFocus(ClientFocus focus);

    /// @brief Drop the focus + tracking state for `peer`.
    void removeFocus(PeerId peer);

    /// @brief Add an explicit-allow-list entry for `peer`.
    void addExplicit(PeerId peer, NetEntityId entity);
    void removeExplicit(PeerId peer, NetEntityId entity);

    /// @brief Recompute visibility for `peer` against the supplied
    /// world-state positions. Updates internal entered/exited bookkeeping.
    VisibilitySet buildVisibleSet(PeerId peer,
                                  std::span<const EntityPosition> world);

    /// @brief How many clients are currently tracked.
    std::size_t focusCount() const noexcept { return focuses_.size(); }

    /// @brief Inspect the focus for `peer`; nullptr if unknown.
    const ClientFocus* focus(PeerId peer) const noexcept;

private:
    struct PerPeer {
        ClientFocus focus;
        std::unordered_set<std::uint64_t> lastVisible;
    };
    std::unordered_map<std::uint32_t, PerPeer> focuses_;
};

} // namespace threadmaxx::network
