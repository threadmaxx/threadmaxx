/// @file InterestManager.cpp

#include "threadmaxx_network/interest.hpp"

#include <utility>

namespace threadmaxx::network {

namespace {

float distSq(float ax, float ay, float az,
             float bx, float by, float bz) noexcept {
    const float dx = ax - bx;
    const float dy = ay - by;
    const float dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

} // namespace

void InterestManager::setFocus(ClientFocus focus) {
    auto& slot = focuses_[focus.peer.value];
    slot.focus = std::move(focus);
}

void InterestManager::removeFocus(PeerId peer) {
    focuses_.erase(peer.value);
}

void InterestManager::addExplicit(PeerId peer, NetEntityId entity) {
    auto& slot = focuses_[peer.value];
    if (!slot.focus.peer.valid()) slot.focus.peer = peer;
    slot.focus.allowList.insert(entity.value);
}

void InterestManager::removeExplicit(PeerId peer, NetEntityId entity) {
    auto it = focuses_.find(peer.value);
    if (it == focuses_.end()) return;
    it->second.focus.allowList.erase(entity.value);
}

VisibilitySet InterestManager::buildVisibleSet(PeerId peer,
        std::span<const EntityPosition> world) {
    VisibilitySet out;
    auto it = focuses_.find(peer.value);
    if (it == focuses_.end()) return out;

    auto& slot = it->second;
    const auto& focus = slot.focus;
    const float r2 = focus.config.radius * focus.config.radius;
    std::unordered_set<std::uint64_t> nowVisible;
    nowVisible.reserve(world.size() / 4);

    for (const auto& e : world) {
        const bool explicitHit =
            focus.allowList.find(e.id.value) != focus.allowList.end();
        const bool inRange =
            focus.config.radius > 0.0f &&
            distSq(focus.x, focus.y, focus.z, e.x, e.y, e.z) <= r2;
        if (!(explicitHit || inRange)) continue;
        nowVisible.insert(e.id.value);
        out.visible.push_back(e.id);
    }

    for (auto id : nowVisible) {
        if (slot.lastVisible.find(id) == slot.lastVisible.end()) {
            out.entered.push_back(NetEntityId{id});
        }
    }
    for (auto id : slot.lastVisible) {
        if (nowVisible.find(id) == nowVisible.end()) {
            out.exited.push_back(NetEntityId{id});
        }
    }
    slot.lastVisible = std::move(nowVisible);
    return out;
}

const ClientFocus* InterestManager::focus(PeerId peer) const noexcept {
    auto it = focuses_.find(peer.value);
    return it != focuses_.end() ? &it->second.focus : nullptr;
}

} // namespace threadmaxx::network
