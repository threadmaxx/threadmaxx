#pragma once

/// @file world.hpp
/// @brief M6 — Adapter between the engine's `threadmaxx::WorldSnapshot`
/// and the migration library's `RecordSet`. Opt-in: the header
/// includes engine types and is only compiled when the engine target
/// is available (gated in CMake with
/// THREADMAXX_MIGRATION_HAS_ENGINE_BRIDGE=1).
///
/// Per-entity layout: one `Record` per index in the source snapshot
/// (so one entity = one record). `typeName = "Entity"`. The snapshot's
/// 13 dense arrays + the per-entity ComponentSet mask are encoded as
/// named fields on the record.
///
/// Field naming (all suffix-less for the entity-as-a-whole shape):
///   - "handle"          [u32 generation][u32 index]
///   - "mask"            [u64]
///   - "transform"       [Vec3 pos][Quat orient][Vec3 scale]
///   - "velocity"        [Vec3 lin][Vec3 ang]
///   - "acceleration"    [Vec3 lin][Vec3 ang]
///   - "renderTag"       [i32 mesh][i32 mat][u32 flags]
///   - "userData"        [u64]
///   - "parent"          [u32 gen][u32 idx][Transform localOffset]
///   - "health"          [f32 current][f32 max]
///   - "faction"         [u32]
///   - "animationStateRef" [u32 graphIdx][u32 graphGen][u32 state][f32 t]
///   - "physicsBodyRef"  [u64]
///   - "navAgentRef"     [u64]
///   - "boundingVolume"  [Vec3 min][Vec3 max]
///
/// Round-trip contract: exporting then importing an unmigrated
/// RecordSet produces a `WorldSnapshot` byte-for-byte equal to the
/// source. The migration pipeline can sit between the two stages.

#include "savefile.hpp"

#include <threadmaxx/Serialization.hpp>

namespace threadmaxx::migration {

/// @brief Export an engine `WorldSnapshot` to a `RecordSet`. Every
/// entity becomes one `Record { typeName="Entity", stableId=index,
/// sourceVersion=metadata.schemaVersion }`. The presence-mask is
/// carried explicitly; absent components are not encoded.
[[nodiscard]] RecordSet
exportSnapshot(const threadmaxx::WorldSnapshot& snapshot,
               const SaveMetadata& metadata = {});

/// @brief Inverse of `exportSnapshot`. Returns `true` on success.
/// Records with `typeName != "Entity"` are skipped (the M7 codec
/// bridge will pick them up). Missing fields default to zeroed
/// component values (the presence mask still controls whether the
/// engine treats them as live).
[[nodiscard]] bool
importSnapshot(const RecordSet& set,
               threadmaxx::WorldSnapshot& out);

/// @brief Stable type-name for whole-entity records. Used by the
/// pipeline (M5) and registry (M3) when the engine bridge is in play.
inline constexpr const char* kEntityRecordTypeName = "Entity";

} // namespace threadmaxx::migration
