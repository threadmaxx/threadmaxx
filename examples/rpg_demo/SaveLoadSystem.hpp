#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

#include <filesystem>

namespace rpg {

/// F5 saves a `WorldSnapshot` to disk; F9 restores it. Built-in
/// components only — UserComponents (CubeRender / NpcState / etc.) are
/// recomputed from the on-disk masks via a small heuristic: any entity
/// with Faction is given a default NpcState; the player entity is
/// recognized by `Faction.id == kFactionPlayer`.
class SaveLoadSystem : public threadmaxx::ISystem {
public:
    SaveLoadSystem(WorldState* worldState,
                   UserComponentIds* ids,
                   std::filesystem::path savePath);

    const char* name() const noexcept override { return "save-load"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::all(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::all(); }
    void update(threadmaxx::SystemContext&) override {}
    void preStep(threadmaxx::SystemContext& ctx) override;

private:
    void save_(threadmaxx::SystemContext& ctx);
    void load_(threadmaxx::SystemContext& ctx);

    WorldState*           worldState_ = nullptr;
    UserComponentIds*     ids_        = nullptr;
    std::filesystem::path savePath_;
};

} // namespace rpg
