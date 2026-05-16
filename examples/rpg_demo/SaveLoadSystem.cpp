#include "SaveLoadSystem.hpp"

#include "Input.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Serialization.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rpg {

namespace {

// §3.11.3 batch D3 — game-specific save magic + version. Distinct from
// the engine's `WorldSnapshot` magic so a malformed file is detected
// before the engine's deserialize runs.
constexpr std::uint32_t kRpgSaveMagic   = 0x53475052u;  // 'RPGS' LE
constexpr std::uint32_t kRpgSaveVersion = 1u;

template <typename T>
void writePod(std::ostream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T>
bool readPod(std::istream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<std::size_t>(is.gcount()) == sizeof(T);
}

void writeStr(std::ostream& os, std::string_view s) {
    const std::uint32_t n = static_cast<std::uint32_t>(s.size());
    writePod(os, n);
    if (n) os.write(s.data(), n);
}
bool readStr(std::istream& is, std::string& out) {
    std::uint32_t n = 0;
    if (!readPod(is, n)) return false;
    out.resize(n);
    if (n) is.read(out.data(), n);
    return static_cast<std::size_t>(is.gcount()) == n;
}

// Generic per-user-component section writer / reader. `name` is the
// game-side type name (round-tripped only for diagnostics); `id` is the
// runtime UserComponentId; T is the POD type.
template <typename T>
void writeSection(std::ostream& os, std::string_view name,
                  const std::vector<std::pair<threadmaxx::EntityHandle, T>>& items,
                  const std::unordered_map<threadmaxx::EntityHandle, std::uint32_t>& handleToSnap) {
    writeStr(os, name);
    const std::uint32_t stride = sizeof(T);
    writePod(os, stride);
    // Pre-filter to entries whose handle resolves to a snap index.
    std::vector<std::pair<std::uint32_t, T>> resolved;
    resolved.reserve(items.size());
    for (const auto& [h, v] : items) {
        auto it = handleToSnap.find(h);
        if (it == handleToSnap.end()) continue;
        resolved.emplace_back(it->second, v);
    }
    const std::uint64_t count = resolved.size();
    writePod(os, count);
    for (const auto& [idx, v] : resolved) {
        writePod(os, idx);
        os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
}

template <typename T>
bool readSection(std::istream& is, std::string& outName,
                 std::vector<std::pair<std::uint32_t, T>>& out) {
    out.clear();
    if (!readStr(is, outName)) return false;
    std::uint32_t stride = 0;
    if (!readPod(is, stride)) return false;
    if (stride != sizeof(T)) return false;  // wrong type or version mismatch
    std::uint64_t count = 0;
    if (!readPod(is, count)) return false;
    out.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint32_t idx = 0;
        T v{};
        if (!readPod(is, idx)) return false;
        is.read(reinterpret_cast<char*>(&v), sizeof(T));
        if (static_cast<std::size_t>(is.gcount()) != sizeof(T)) return false;
        out.emplace_back(idx, v);
    }
    return true;
}

} // namespace

SaveLoadSystem::SaveLoadSystem(threadmaxx::Engine* engine,
                               WorldState* worldState,
                               UserComponentIds* ids,
                               std::filesystem::path savePath)
    : engine_(engine), worldState_(worldState), ids_(ids),
      savePath_(std::move(savePath)) {}

void SaveLoadSystem::preStep(threadmaxx::SystemContext& ctx) {
    const std::uint32_t edges = input().edges.load(std::memory_order_acquire);
    if (edges & kEdgeSaveQuick) {
        input().edges.fetch_and(~kEdgeSaveQuick, std::memory_order_acq_rel);
        saveSync_(ctx);
    }
    if (edges & kEdgeSaveAsync) {
        input().edges.fetch_and(~kEdgeSaveAsync, std::memory_order_acq_rel);
        saveAsync_(ctx);
    }
    if (edges & kEdgeLoadQuick) {
        input().edges.fetch_and(~kEdgeLoadQuick, std::memory_order_acq_rel);
        load_(ctx);
    }
}

// ----- Capture -------------------------------------------------------------

void SaveLoadSystem::captureUserComponents_(const threadmaxx::World& w,
                                            UserCompCapture& out) const {
    out.cubes.clear();
    out.npcs.clear();
    out.players.clear();
    out.pickups.clear();
    out.swords.clear();
    out.anims.clear();
    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t i = 0; i < chunkCount; ++i) {
        const auto& chunk = w.archetypeChunk(i);
        const auto n = chunk.entities.size();
        if (n == 0) continue;
        // §3.11.3 batch D3 — walk each chunk's user columns. The
        // typed `user::chunkSpan<T>` aliases the column bytes; we
        // pair each row's value with its handle so the write path
        // can resolve to a snap index later.
        if (chunk.mask.has(ids_->cubeRender.componentBit())) {
            auto sp = threadmaxx::user::chunkSpan<CubeRender>(chunk, ids_->cubeRender);
            for (std::size_t r = 0; r < n; ++r)
                out.cubes.emplace_back(chunk.entities[r], sp[r]);
        }
        if (chunk.mask.has(ids_->npcState.componentBit())) {
            auto sp = threadmaxx::user::chunkSpan<NpcState>(chunk, ids_->npcState);
            for (std::size_t r = 0; r < n; ++r)
                out.npcs.emplace_back(chunk.entities[r], sp[r]);
        }
        if (chunk.mask.has(ids_->playerState.componentBit())) {
            auto sp = threadmaxx::user::chunkSpan<PlayerState>(chunk, ids_->playerState);
            for (std::size_t r = 0; r < n; ++r)
                out.players.emplace_back(chunk.entities[r], sp[r]);
        }
        if (chunk.mask.has(ids_->pickup.componentBit())) {
            auto sp = threadmaxx::user::chunkSpan<Pickup>(chunk, ids_->pickup);
            for (std::size_t r = 0; r < n; ++r)
                out.pickups.emplace_back(chunk.entities[r], sp[r]);
        }
        if (chunk.mask.has(ids_->swordTag.componentBit())) {
            auto sp = threadmaxx::user::chunkSpan<SwordTag>(chunk, ids_->swordTag);
            for (std::size_t r = 0; r < n; ++r)
                out.swords.emplace_back(chunk.entities[r], sp[r]);
        }
        // §3.11.6 batch D6 — animation state.
        if (chunk.mask.has(ids_->animState.componentBit())) {
            auto sp = threadmaxx::user::chunkSpan<AnimState>(chunk, ids_->animState);
            for (std::size_t r = 0; r < n; ++r)
                out.anims.emplace_back(chunk.entities[r], sp[r]);
        }
    }
    out.playerHandle = worldState_->player;
    out.swordHandle  = worldState_->sword;
    out.totalKills   = worldState_->totalKills;
    out.sunAngle     = worldState_->sunAngle;
}

// ----- Write -------------------------------------------------------------

void SaveLoadSystem::writeSave_(const std::filesystem::path& path,
                                const threadmaxx::WorldSnapshot& built,
                                const UserCompCapture& uc) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::fprintf(stderr, "[save] cannot open %s for writing\n",
                     path.string().c_str());
        return;
    }
    writePod(f, kRpgSaveMagic);
    writePod(f, kRpgSaveVersion);
    threadmaxx::serialize(f, built);

    // Build the handle→snap-index map from the built snapshot. Worker-
    // thread safe because both `built` and `uc` are now owned values.
    std::unordered_map<threadmaxx::EntityHandle, std::uint32_t> handleToSnap;
    handleToSnap.reserve(built.entities.size());
    for (std::uint32_t i = 0; i < built.entities.size(); ++i) {
        handleToSnap.emplace(built.entities[i], i);
    }

    // §3.11.6 batch D6 — section count bumped to 6 (added AnimState).
    // Older saves with kSections=5 are no longer loadable; the file
    // format isn't versioned per-section so we treat the bump as a
    // breaking change for save files written before D6.
    const std::uint32_t kSections = 6;
    writePod(f, kSections);
    writeSection(f, "CubeRender",  uc.cubes,   handleToSnap);
    writeSection(f, "NpcState",    uc.npcs,    handleToSnap);
    writeSection(f, "PlayerState", uc.players, handleToSnap);
    writeSection(f, "Pickup",      uc.pickups, handleToSnap);
    writeSection(f, "SwordTag",    uc.swords,  handleToSnap);
    writeSection(f, "AnimState",   uc.anims,   handleToSnap);

    auto resolveOpt = [&](threadmaxx::EntityHandle h) -> std::uint32_t {
        auto it = handleToSnap.find(h);
        return it == handleToSnap.end()
            ? std::numeric_limits<std::uint32_t>::max() : it->second;
    };
    writePod(f, resolveOpt(uc.playerHandle));
    writePod(f, resolveOpt(uc.swordHandle));
    writePod(f, uc.totalKills);
    writePod(f, uc.sunAngle);

    std::printf("[save] wrote %zu entities + %zu user-comp values to %s\n",
                built.size(),
                uc.cubes.size() + uc.npcs.size() + uc.players.size()
                  + uc.pickups.size() + uc.swords.size(),
                path.string().c_str());
}

// ----- Save: sync (F5) ---------------------------------------------------

void SaveLoadSystem::saveSync_(threadmaxx::SystemContext& ctx) {
    UserCompCapture uc;
    captureUserComponents_(ctx.world(), uc);
    const threadmaxx::WorldSnapshot built = ctx.world().snapshot();
    writeSave_(savePath_, built, uc);
}

// ----- Save: async (F8) --------------------------------------------------

void SaveLoadSystem::saveAsync_(threadmaxx::SystemContext& ctx) {
    // §3.11.3 batch D3 — capture user components on the sim thread
    // (matches the snapshotAsync semantics: snapshot copy is sync, I/O
    // is async). The shared_ptr keeps the capture alive across the
    // worker boundary without copy churn.
    auto uc = std::make_shared<UserCompCapture>();
    captureUserComponents_(ctx.world(), *uc);
    const auto path = savePath_;
    engine_->snapshotAsync([uc = std::move(uc), path](
                           threadmaxx::WorldSnapshot built) mutable {
        // Background thread: write the combined save.
        writeSave_(path, built, *uc);
        std::printf("[save] async write completed\n");
    });
    std::printf("[save] async capture queued (sim thread continues)\n");
}

// ----- Read --------------------------------------------------------------

bool SaveLoadSystem::readSave_(const std::filesystem::path& path,
                               threadmaxx::WorldSnapshot& outBuilt,
                               UserCompCapture& outUc,
                               std::uint32_t& outPlayerSnapIdx,
                               std::uint32_t& outSwordSnapIdx) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::uint32_t magic = 0, version = 0;
    if (!readPod(f, magic) || magic != kRpgSaveMagic) return false;
    if (!readPod(f, version) || version != kRpgSaveVersion) return false;
    if (!threadmaxx::deserialize(f, outBuilt)) return false;

    std::uint32_t sectionCount = 0;
    if (!readPod(f, sectionCount)) return false;
    for (std::uint32_t s = 0; s < sectionCount; ++s) {
        std::string name;
        // Peek at the section name to dispatch to the right typed reader.
        // We could lookahead but readStr already consumes; route by name.
        if (s == 0) {
            std::vector<std::pair<std::uint32_t, CubeRender>> v;
            if (!readSection<CubeRender>(f, name, v)) return false;
            outUc.cubes.clear();
            for (auto& [i, val] : v)
                outUc.cubes.emplace_back(threadmaxx::EntityHandle{i, 0}, val);
        } else if (s == 1) {
            std::vector<std::pair<std::uint32_t, NpcState>> v;
            if (!readSection<NpcState>(f, name, v)) return false;
            outUc.npcs.clear();
            for (auto& [i, val] : v)
                outUc.npcs.emplace_back(threadmaxx::EntityHandle{i, 0}, val);
        } else if (s == 2) {
            std::vector<std::pair<std::uint32_t, PlayerState>> v;
            if (!readSection<PlayerState>(f, name, v)) return false;
            outUc.players.clear();
            for (auto& [i, val] : v)
                outUc.players.emplace_back(threadmaxx::EntityHandle{i, 0}, val);
        } else if (s == 3) {
            std::vector<std::pair<std::uint32_t, Pickup>> v;
            if (!readSection<Pickup>(f, name, v)) return false;
            outUc.pickups.clear();
            for (auto& [i, val] : v)
                outUc.pickups.emplace_back(threadmaxx::EntityHandle{i, 0}, val);
        } else if (s == 4) {
            std::vector<std::pair<std::uint32_t, SwordTag>> v;
            if (!readSection<SwordTag>(f, name, v)) return false;
            outUc.swords.clear();
            for (auto& [i, val] : v)
                outUc.swords.emplace_back(threadmaxx::EntityHandle{i, 0}, val);
        } else if (s == 5) {
            // §3.11.6 batch D6 — animation state.
            std::vector<std::pair<std::uint32_t, AnimState>> v;
            if (!readSection<AnimState>(f, name, v)) return false;
            outUc.anims.clear();
            for (auto& [i, val] : v)
                outUc.anims.emplace_back(threadmaxx::EntityHandle{i, 0}, val);
        } else {
            // Unknown section — skip by reading the standard header
            // then jumping `stride * count` bytes ahead.
            std::uint32_t stride = 0;
            std::uint64_t count = 0;
            if (!readStr(f, name) || !readPod(f, stride) || !readPod(f, count))
                return false;
            f.seekg(static_cast<std::streamoff>((sizeof(std::uint32_t) + stride) * count),
                    std::ios::cur);
        }
    }
    if (!readPod(f, outPlayerSnapIdx)) return false;
    if (!readPod(f, outSwordSnapIdx)) return false;
    if (!readPod(f, outUc.totalKills)) return false;
    if (!readPod(f, outUc.sunAngle)) return false;
    return true;
}

// ----- Load (F9) ---------------------------------------------------------

void SaveLoadSystem::load_(threadmaxx::SystemContext& ctx) {
    threadmaxx::WorldSnapshot built;
    UserCompCapture uc;
    std::uint32_t playerSnapIdx = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t swordSnapIdx  = std::numeric_limits<std::uint32_t>::max();
    if (!readSave_(savePath_, built, uc, playerSnapIdx, swordSnapIdx)) {
        std::fprintf(stderr, "[load] %s is not a valid rpg_demo save\n",
                     savePath_.string().c_str());
        return;
    }
    const std::size_t n = built.size();
    if (n == 0) {
        std::printf("[load] empty save; world unchanged\n");
        return;
    }

    // §3.11.3 batch D3 — reserve N handles up front so we can populate
    // Parent.parent fields with the NEW handles before commit fires.
    std::vector<threadmaxx::EntityHandle> newHandles(n);
    const std::uint32_t got = engine_->reserveEntityHandles(
        static_cast<std::uint32_t>(n),
        std::span<threadmaxx::EntityHandle>(newHandles.data(), newHandles.size()));
    if (got != n) {
        std::fprintf(stderr, "[load] failed to reserve %zu handles (got %u)\n",
                     n, got);
        return;
    }

    // Snapshot every live entity so we can issue destroys.
    const auto liveSpan = ctx.world().entities();
    std::vector<threadmaxx::EntityHandle> liveEntities(
        liveSpan.begin(), liveSpan.end());

    // Update world state from the save. Player + sword handles set
    // post-spawn from `newHandles`.
    worldState_->totalKills = uc.totalKills;
    worldState_->sunAngle   = uc.sunAngle;
    worldState_->player = (playerSnapIdx < n)
        ? newHandles[playerSnapIdx] : threadmaxx::EntityHandle{};
    worldState_->sword  = (swordSnapIdx < n)
        ? newHandles[swordSnapIdx] : threadmaxx::EntityHandle{};

    // Move heavy data into the single() callback to keep the sim
    // thread's preStep cheap. CommandBuffer apply happens at the
    // end of this system's wave.
    auto builtPtr = std::make_shared<threadmaxx::WorldSnapshot>(std::move(built));
    auto ucPtr    = std::make_shared<UserCompCapture>(std::move(uc));
    auto handlesPtr = std::make_shared<std::vector<threadmaxx::EntityHandle>>(
        std::move(newHandles));
    auto livePtr  = std::make_shared<std::vector<threadmaxx::EntityHandle>>(
        std::move(liveEntities));
    auto ids = ids_;

    ctx.single([builtPtr, ucPtr, handlesPtr, livePtr, ids]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        const auto& built = *builtPtr;
        const auto& uc    = *ucPtr;
        const auto& nh    = *handlesPtr;

        // 1) Destroy every live entity. Submission-order applies, so
        //    the destroys land before the spawns below.
        for (auto e : *livePtr) cb.destroy(e);

        // 2) Re-spawn each snapshot entity into its reserved handle.
        const std::size_t n = built.size();
        for (std::size_t i = 0; i < n; ++i) {
            threadmaxx::Bundle b{};
            const auto& mask = built.masks[i];
            // Always copy the dense values; the mask gates which ones
            // are actually attached.
            b.transform        = built.transforms[i];
            b.velocity         = built.velocities[i];
            b.renderTag        = built.renderTags[i];
            b.userData         = built.userData[i];
            b.acceleration     = built.accelerations[i];
            b.health           = built.healths[i];
            b.faction          = built.factions[i];
            b.animationState   = built.animationStates[i];
            b.physicsBody      = built.physicsBodies[i];
            b.navAgent         = built.navAgents[i];
            b.boundingVolume   = built.boundingVolumes[i];
            // Parent: translate the saved parent handle through the
            // snap-index map (which we built above using saved
            // entities). If the parent isn't in the snapshot, drop the
            // Parent bit so the child becomes a root.
            const auto& savedParent = built.parents[i];
            threadmaxx::ComponentSet maskOut = mask;
            if (mask.has(threadmaxx::Component::Parent)) {
                // Find the saved parent's snap index by scanning the
                // built.entities vector. O(N²) but N≈150 in this demo.
                std::uint32_t parentIdx =
                    std::numeric_limits<std::uint32_t>::max();
                for (std::uint32_t j = 0; j < built.entities.size(); ++j) {
                    if (built.entities[j] == savedParent.parent) {
                        parentIdx = j; break;
                    }
                }
                threadmaxx::Parent newP = savedParent;
                if (parentIdx != std::numeric_limits<std::uint32_t>::max()) {
                    newP.parent = nh[parentIdx];
                } else {
                    newP.parent = threadmaxx::EntityHandle{};
                    maskOut.remove(threadmaxx::Component::Parent);
                }
                b.parent = newP;
            }
            b.initialMask = maskOut;
            cb.spawnBundle(nh[i], b);
        }

        // 3) Re-attach user components.
        for (const auto& [_, v] : uc.cubes) {
            // _ is the saved handle (with generation lost on rehydrate);
            // index resolution happens implicitly: the read path emits
            // EntityHandle{snap_idx, 0}, so handle.index IS the snap idx.
            (void)_;
        }
        // CubeRender
        for (const auto& [savedHandle, val] : uc.cubes) {
            const auto idx = savedHandle.index;
            if (idx < nh.size())
                threadmaxx::addUserComponent(cb, ids->cubeRender, nh[idx], val);
        }
        for (const auto& [savedHandle, val] : uc.npcs) {
            const auto idx = savedHandle.index;
            if (idx < nh.size())
                threadmaxx::addUserComponent(cb, ids->npcState, nh[idx], val);
        }
        for (const auto& [savedHandle, val] : uc.players) {
            const auto idx = savedHandle.index;
            if (idx < nh.size())
                threadmaxx::addUserComponent(cb, ids->playerState, nh[idx], val);
        }
        for (const auto& [savedHandle, val] : uc.pickups) {
            const auto idx = savedHandle.index;
            if (idx < nh.size())
                threadmaxx::addUserComponent(cb, ids->pickup, nh[idx], val);
        }
        for (const auto& [savedHandle, val] : uc.swords) {
            const auto idx = savedHandle.index;
            if (idx < nh.size())
                threadmaxx::addUserComponent(cb, ids->swordTag, nh[idx], val);
        }
        // §3.11.6 batch D6 — re-attach AnimState so NPC bobbing
        // resumes after F9.
        for (const auto& [savedHandle, val] : uc.anims) {
            const auto idx = savedHandle.index;
            if (idx < nh.size())
                threadmaxx::addUserComponent(cb, ids->animState, nh[idx], val);
        }
    });

    std::printf("[load] restored %zu entities (player=%u sword=%u kills=%u)\n",
                n, playerSnapIdx, swordSnapIdx, worldState_->totalKills);
}

} // namespace rpg
