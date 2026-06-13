/// @file WorldSnapshotMigrator.cpp
/// @brief M6 — exportSnapshot / importSnapshot between the engine's
/// WorldSnapshot and the migration RecordSet.

#include <threadmaxx_migration/world.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>

#include <cstring>
#include <utility>

namespace threadmaxx::migration {

namespace {

template <class T>
FieldValue packPod(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    FieldValue v{};
    v.bytes.resize(sizeof(T));
    std::memcpy(v.bytes.data(), &value, sizeof(T));
    return v;
}

template <class T>
bool unpackPod(const FieldValue& v, T& out) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (v.bytes.size() != sizeof(T)) return false;
    std::memcpy(&out, v.bytes.data(), sizeof(T));
    return true;
}

const RecordField*
findField(const Record& rec, std::string_view name) noexcept {
    for (const auto& f : rec.fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

// PerEntity scratch struct mirroring the masked snapshot rows.
struct EntityRow {
    threadmaxx::EntityHandle      handle{};
    threadmaxx::ComponentSet      mask{};
    threadmaxx::Transform         transform{};
    threadmaxx::Velocity          velocity{};
    threadmaxx::Acceleration      acceleration{};
    threadmaxx::RenderTag         renderTag{};
    threadmaxx::UserData          userData{};
    threadmaxx::Parent            parent{};
    threadmaxx::Health            health{};
    threadmaxx::Faction           faction{};
    threadmaxx::AnimationStateRef animationStateRef{};
    threadmaxx::PhysicsBodyRef    physicsBodyRef{};
    threadmaxx::NavAgentRef       navAgentRef{};
    threadmaxx::BoundingVolume    boundingVolume{};
};

} // namespace

RecordSet exportSnapshot(const threadmaxx::WorldSnapshot& s,
                         const SaveMetadata& metadata) {
    RecordSet out{};
    out.metadata = metadata;
    out.records.reserve(s.entities.size());

    // Indices into the dense arrays move independently of `i` since
    // each component vector is sized to the entities that have the
    // bit set, not to the total entity count. The snapshot guarantees
    // each dense vector matches one-to-one with the source slot order
    // for entities with that mask bit. We track per-component cursors.
    std::size_t cursorTransform = 0;
    std::size_t cursorVelocity = 0;
    std::size_t cursorRenderTag = 0;
    std::size_t cursorUserData = 0;
    std::size_t cursorAcceleration = 0;
    std::size_t cursorParent = 0;
    std::size_t cursorHealth = 0;
    std::size_t cursorFaction = 0;
    std::size_t cursorAnimRef = 0;
    std::size_t cursorPhysicsRef = 0;
    std::size_t cursorNavRef = 0;
    std::size_t cursorBoundingVolume = 0;

    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        Record rec{};
        rec.typeName = kEntityRecordTypeName;
        rec.stableId = static_cast<std::uint64_t>(i);
        rec.sourceVersion = metadata.schemaVersion;

        const auto mask = s.masks[i];
        rec.fields.push_back({"handle", packPod(s.entities[i])});
        rec.fields.push_back({"mask", packPod(mask.bits())});

        if (mask.has(threadmaxx::Component::Transform) &&
            cursorTransform < s.transforms.size()) {
            rec.fields.push_back({"transform",
                                  packPod(s.transforms[cursorTransform++])});
        }
        if (mask.has(threadmaxx::Component::Velocity) &&
            cursorVelocity < s.velocities.size()) {
            rec.fields.push_back({"velocity",
                                  packPod(s.velocities[cursorVelocity++])});
        }
        if (mask.has(threadmaxx::Component::RenderTag) &&
            cursorRenderTag < s.renderTags.size()) {
            rec.fields.push_back({"renderTag",
                                  packPod(s.renderTags[cursorRenderTag++])});
        }
        if (mask.has(threadmaxx::Component::UserData) &&
            cursorUserData < s.userData.size()) {
            rec.fields.push_back({"userData",
                                  packPod(s.userData[cursorUserData++])});
        }
        if (mask.has(threadmaxx::Component::Acceleration) &&
            cursorAcceleration < s.accelerations.size()) {
            rec.fields.push_back({"acceleration",
                                  packPod(s.accelerations[cursorAcceleration++])});
        }
        if (mask.has(threadmaxx::Component::Parent) &&
            cursorParent < s.parents.size()) {
            rec.fields.push_back({"parent",
                                  packPod(s.parents[cursorParent++])});
        }
        if (mask.has(threadmaxx::Component::Health) &&
            cursorHealth < s.healths.size()) {
            rec.fields.push_back({"health",
                                  packPod(s.healths[cursorHealth++])});
        }
        if (mask.has(threadmaxx::Component::Faction) &&
            cursorFaction < s.factions.size()) {
            rec.fields.push_back({"faction",
                                  packPod(s.factions[cursorFaction++])});
        }
        if (mask.has(threadmaxx::Component::AnimationStateRef) &&
            cursorAnimRef < s.animationStates.size()) {
            rec.fields.push_back({"animationStateRef",
                                  packPod(s.animationStates[cursorAnimRef++])});
        }
        if (mask.has(threadmaxx::Component::PhysicsBodyRef) &&
            cursorPhysicsRef < s.physicsBodies.size()) {
            rec.fields.push_back({"physicsBodyRef",
                                  packPod(s.physicsBodies[cursorPhysicsRef++])});
        }
        if (mask.has(threadmaxx::Component::NavAgentRef) &&
            cursorNavRef < s.navAgents.size()) {
            rec.fields.push_back({"navAgentRef",
                                  packPod(s.navAgents[cursorNavRef++])});
        }
        if (mask.has(threadmaxx::Component::BoundingVolume) &&
            cursorBoundingVolume < s.boundingVolumes.size()) {
            rec.fields.push_back({"boundingVolume",
                                  packPod(s.boundingVolumes[cursorBoundingVolume++])});
        }

        out.records.push_back(std::move(rec));
    }

    return out;
}

bool importSnapshot(const RecordSet& set,
                    threadmaxx::WorldSnapshot& out) {
    out = threadmaxx::WorldSnapshot{};
    for (const auto& rec : set.records) {
        if (rec.typeName != kEntityRecordTypeName) continue;

        threadmaxx::EntityHandle handle{};
        if (const auto* f = findField(rec, "handle"); f) {
            if (!unpackPod(f->value, handle)) return false;
        }
        std::uint64_t maskBits = 0;
        if (const auto* f = findField(rec, "mask"); f) {
            if (!unpackPod(f->value, maskBits)) return false;
        }
        // The public ComponentSet API doesn't expose a from-bits ctor,
        // so reconstruct by iterating allocated Component bit positions.
        threadmaxx::ComponentSet mask;
        constexpr threadmaxx::Component kAllComponents[] = {
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::RenderTag,
            threadmaxx::Component::UserData,
            threadmaxx::Component::EntityStructural,
            threadmaxx::Component::Acceleration,
            threadmaxx::Component::Parent,
            threadmaxx::Component::Health,
            threadmaxx::Component::Faction,
            threadmaxx::Component::AnimationStateRef,
            threadmaxx::Component::PhysicsBodyRef,
            threadmaxx::Component::NavAgentRef,
            threadmaxx::Component::BoundingVolume,
            threadmaxx::Component::StaticTag,
            threadmaxx::Component::DisabledTag,
            threadmaxx::Component::DestroyedTag,
        };
        for (auto c : kAllComponents) {
            if (maskBits & static_cast<std::uint64_t>(c)) mask.add(c);
        }

        out.entities.push_back(handle);
        out.masks.push_back(mask);

        if (mask.has(threadmaxx::Component::Transform)) {
            threadmaxx::Transform v{};
            if (const auto* f = findField(rec, "transform"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.transforms.push_back(v);
        }
        if (mask.has(threadmaxx::Component::Velocity)) {
            threadmaxx::Velocity v{};
            if (const auto* f = findField(rec, "velocity"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.velocities.push_back(v);
        }
        if (mask.has(threadmaxx::Component::RenderTag)) {
            threadmaxx::RenderTag v{};
            if (const auto* f = findField(rec, "renderTag"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.renderTags.push_back(v);
        }
        if (mask.has(threadmaxx::Component::UserData)) {
            threadmaxx::UserData v{};
            if (const auto* f = findField(rec, "userData"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.userData.push_back(v);
        }
        if (mask.has(threadmaxx::Component::Acceleration)) {
            threadmaxx::Acceleration v{};
            if (const auto* f = findField(rec, "acceleration"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.accelerations.push_back(v);
        }
        if (mask.has(threadmaxx::Component::Parent)) {
            threadmaxx::Parent v{};
            if (const auto* f = findField(rec, "parent"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.parents.push_back(v);
        }
        if (mask.has(threadmaxx::Component::Health)) {
            threadmaxx::Health v{};
            if (const auto* f = findField(rec, "health"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.healths.push_back(v);
        }
        if (mask.has(threadmaxx::Component::Faction)) {
            threadmaxx::Faction v{};
            if (const auto* f = findField(rec, "faction"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.factions.push_back(v);
        }
        if (mask.has(threadmaxx::Component::AnimationStateRef)) {
            threadmaxx::AnimationStateRef v{};
            if (const auto* f = findField(rec, "animationStateRef"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.animationStates.push_back(v);
        }
        if (mask.has(threadmaxx::Component::PhysicsBodyRef)) {
            threadmaxx::PhysicsBodyRef v{};
            if (const auto* f = findField(rec, "physicsBodyRef"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.physicsBodies.push_back(v);
        }
        if (mask.has(threadmaxx::Component::NavAgentRef)) {
            threadmaxx::NavAgentRef v{};
            if (const auto* f = findField(rec, "navAgentRef"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.navAgents.push_back(v);
        }
        if (mask.has(threadmaxx::Component::BoundingVolume)) {
            threadmaxx::BoundingVolume v{};
            if (const auto* f = findField(rec, "boundingVolume"); f) {
                if (!unpackPod(f->value, v)) return false;
            }
            out.boundingVolumes.push_back(v);
        }
    }
    return true;
}

} // namespace threadmaxx::migration
