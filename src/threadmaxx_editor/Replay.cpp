/// @file Replay.cpp
/// @brief E15 — capture/replay surface over `WorldSnapshot` streams.

#include "threadmaxx_editor/replay.hpp"

#include <threadmaxx/Components.hpp>

#include <algorithm>
#include <istream>
#include <ostream>
#include <string>

namespace threadmaxx::editor {

namespace {

struct BitName {
    threadmaxx::Component bit;
    const char*           name;
};

constexpr BitName kComponentNames[] = {
    {threadmaxx::Component::Transform,         "Transform"},
    {threadmaxx::Component::Velocity,          "Velocity"},
    {threadmaxx::Component::RenderTag,         "RenderTag"},
    {threadmaxx::Component::UserData,          "UserData"},
    {threadmaxx::Component::Acceleration,      "Acceleration"},
    {threadmaxx::Component::Parent,            "Parent"},
    {threadmaxx::Component::Health,            "Health"},
    {threadmaxx::Component::Faction,           "Faction"},
    {threadmaxx::Component::AnimationStateRef, "AnimationStateRef"},
    {threadmaxx::Component::PhysicsBodyRef,    "PhysicsBodyRef"},
    {threadmaxx::Component::NavAgentRef,       "NavAgentRef"},
    {threadmaxx::Component::BoundingVolume,    "BoundingVolume"},
    {threadmaxx::Component::StaticTag,         "StaticTag"},
    {threadmaxx::Component::DisabledTag,       "DisabledTag"},
    {threadmaxx::Component::DestroyedTag,      "DestroyedTag"},
};

std::vector<std::string> componentNamesFor(threadmaxx::ComponentSet mask) {
    std::vector<std::string> out;
    out.reserve(4);
    for (const auto& e : kComponentNames) {
        if (mask.has(e.bit)) out.emplace_back(e.name);
    }
    return out;
}

template <class T>
void writePod(std::ostream& os, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <class T>
bool readPod(std::istream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<std::size_t>(is.gcount()) == sizeof(T);
}

} // namespace

void CaptureStream::append(std::uint64_t tick,
                           threadmaxx::WorldSnapshot snapshot) {
    frames_.push_back(CaptureFrame{tick, std::move(snapshot)});
}

void CaptureStream::save(std::ostream& out) const {
    writePod(out, kCaptureStreamMagic);
    writePod(out, kCaptureStreamVersion);
    const std::uint64_t count = frames_.size();
    writePod(out, count);
    for (const auto& f : frames_) {
        writePod(out, f.tick);
        threadmaxx::serialize(out, f.snapshot);
    }
}

bool CaptureStream::load(std::istream& in) {
    std::uint32_t magic   = 0;
    std::uint32_t version = 0;
    std::uint64_t count   = 0;
    if (!readPod(in, magic)   || magic   != kCaptureStreamMagic)   return false;
    if (!readPod(in, version) || version != kCaptureStreamVersion) return false;
    if (!readPod(in, count)) return false;

    std::vector<CaptureFrame> loaded;
    loaded.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        CaptureFrame f;
        if (!readPod(in, f.tick)) return false;
        if (!threadmaxx::deserialize(in, f.snapshot)) return false;
        loaded.push_back(std::move(f));
    }
    frames_ = std::move(loaded);
    return true;
}

ReplaySession::ReplaySession(const CaptureStream& stream) noexcept
    : stream_(&stream) {}

const threadmaxx::WorldSnapshot* ReplaySession::current() const noexcept {
    if (stream_->empty()) return nullptr;
    return &stream_->frame(cursor_).snapshot;
}

std::uint64_t ReplaySession::currentTick() const noexcept {
    if (stream_->empty()) return 0;
    return stream_->frame(cursor_).tick;
}

void ReplaySession::seek(std::size_t index) noexcept {
    if (stream_->empty()) {
        cursor_ = 0;
        return;
    }
    const auto last = stream_->frameCount() - 1;
    cursor_ = std::min(index, last);
}

void ReplaySession::step(std::int64_t delta) noexcept {
    if (stream_->empty()) {
        cursor_ = 0;
        return;
    }
    const auto last     = stream_->frameCount() - 1;
    const auto cursorI  = static_cast<std::int64_t>(cursor_);
    const auto desired  = cursorI + delta;
    if (desired <= 0) {
        cursor_ = 0;
    } else if (static_cast<std::size_t>(desired) > last) {
        cursor_ = last;
    } else {
        cursor_ = static_cast<std::size_t>(desired);
    }
}

std::vector<EntitySummary> ReplaySession::listEntities() const {
    std::vector<EntitySummary> out;
    if (stream_->empty()) return out;
    const auto& snap = stream_->frame(cursor_).snapshot;
    out.reserve(snap.entities.size());
    for (std::size_t i = 0; i < snap.entities.size(); ++i) {
        EntitySummary s;
        s.handle = snap.entities[i];
        s.label  = std::string("entity#") + std::to_string(s.handle.index);
        if (i < snap.masks.size()) {
            s.components = componentNamesFor(snap.masks[i]);
        }
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace threadmaxx::editor
