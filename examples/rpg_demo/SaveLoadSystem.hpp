// §3.11.3 batch D3 — full save / load with user components.
//
// Three keys:
//   F5  — synchronous quick-save.    Captures world snapshot + all user
//          components + world state to /tmp/rpg_demo_save.bin on the
//          sim thread. Blocks the tick until the write completes.
//   F8  — asynchronous quick-save.   Captures user components + world
//          state synchronously on the sim thread, then uses
//          `Engine::snapshotAsync` to dispatch the built-in
//          WorldSnapshot capture + file write to a background thread.
//          Sim thread keeps stepping.
//   F9  — restore.                   Reads the save file, then queues
//          one commit's worth of `cb.destroy` + `cb.spawnBundle` +
//          `addUserComponent` calls to tear down the live world and
//          rebuild it from the saved state. Uses
//          `Engine::reserveEntityHandles` so Parent references can be
//          translated through the saved-snap-index → new-handle map.
//
// Save file format (game-specific, distinct magic from `WorldSnapshot`'s):
//
//   [magic 'RPGS']  [version: u32]
//   [built-in WorldSnapshot via threadmaxx::serialize]
//   [user-component section count: u32 = 5]
//   For each user component:
//     [name length: u32]  [name bytes]
//     [stride: u32]  [entry count: u64]
//     [entries: (snap_idx: u32, blob[stride]) ...]
//   [world state: player_snap_idx u32, sword_snap_idx u32,
//                  totalKills u32, sunAngle f32]
//
// User-component persistence is the demo's responsibility per §3.1
// batch 6b — the engine's `WorldSnapshot` deliberately covers built-in
// components only. The wire format above is `examples/rpg_demo/`-only;
// the engine never sees it.

#pragma once

#include <threadmaxx/Serialization.hpp>
#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

#include <filesystem>
#include <utility>
#include <vector>

namespace threadmaxx { class Engine; }

namespace rpg {

class SaveLoadSystem : public threadmaxx::ISystem {
public:
    SaveLoadSystem(threadmaxx::Engine* engine,
                   WorldState* worldState,
                   UserComponentIds* ids,
                   std::filesystem::path savePath);

    const char* name() const noexcept override { return "save-load"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::all(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::all(); }
    void update(threadmaxx::SystemContext&) override {}
    void preStep(threadmaxx::SystemContext& ctx) override;

private:
    /// §3.11.3 batch D3 — sim-thread capture of every user-component
    /// value, keyed by EntityHandle. Index resolution against the
    /// built-in WorldSnapshot.entities happens at write time (worker
    /// thread for the async path; sim thread for the sync path).
    struct UserCompCapture {
        std::vector<std::pair<threadmaxx::EntityHandle, CubeRender>>  cubes;
        std::vector<std::pair<threadmaxx::EntityHandle, NpcState>>    npcs;
        std::vector<std::pair<threadmaxx::EntityHandle, PlayerState>> players;
        std::vector<std::pair<threadmaxx::EntityHandle, Pickup>>      pickups;
        std::vector<std::pair<threadmaxx::EntityHandle, SwordTag>>    swords;
        /// §3.11.6 batch D6 — procedural animation state.
        std::vector<std::pair<threadmaxx::EntityHandle, AnimState>>   anims;
        threadmaxx::EntityHandle playerHandle{};
        threadmaxx::EntityHandle swordHandle{};
        std::uint32_t totalKills = 0;
        float         sunAngle   = 0.0f;
    };

    void saveSync_  (threadmaxx::SystemContext& ctx);
    void saveAsync_ (threadmaxx::SystemContext& ctx);
    void load_      (threadmaxx::SystemContext& ctx);

    void captureUserComponents_(const threadmaxx::World& w,
                                UserCompCapture& out) const;
    /// Worker-safe: serializes built-in snapshot + user-comp capture +
    /// world-state to @p path. Builds the snap-index map internally.
    static void writeSave_(const std::filesystem::path& path,
                           const threadmaxx::WorldSnapshot& built,
                           const UserCompCapture& uc);
    /// Returns false on bad magic / version mismatch / truncated file.
    static bool readSave_(const std::filesystem::path& path,
                          threadmaxx::WorldSnapshot& outBuilt,
                          UserCompCapture& outUc,
                          std::uint32_t& outPlayerSnapIdx,
                          std::uint32_t& outSwordSnapIdx);

    threadmaxx::Engine*   engine_     = nullptr;
    WorldState*           worldState_ = nullptr;
    UserComponentIds*     ids_        = nullptr;
    std::filesystem::path savePath_;
};

} // namespace rpg
