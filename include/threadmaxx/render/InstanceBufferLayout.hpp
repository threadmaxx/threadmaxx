#pragma once

#include "DrawItem.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace threadmaxx {

/// Stable, renderer-neutral packing of @ref DrawItem into a GPU-friendly
/// per-instance buffer entry. The struct is `std140`-friendly: 16-byte
/// alignment, field order chosen to avoid implicit padding, total size
/// 128 bytes per instance.
///
/// Field order (offset -> bytes):
/// - `[0..47]`  worldPosition (vec3, 16-byte-aligned), worldOrientation
///              (quat, vec4-packed), worldScale (vec3, 16-byte-aligned).
/// - `[48..63]` materialOverride (vec4).
/// - `[64..71]` meshId / materialId (two int32_t).
/// - `[72..79]` skeletonId / poseRingSlot (two int32_t).
/// - `[80..95]` flags / sortKeyLow / sortKeyHigh / entityIndex
///              (four uint32_t).
/// - `[96..127]` 8 floats of pad — reserved for backend extensions.
///
/// Renderers that need a different layout copy the raw @ref DrawItem
/// fields themselves; this struct is the *default*, the lib does not
/// gate on it.
struct alignas(16) InstanceLayoutEntry {
    float worldPosition[4];     // xyz + pad
    float worldOrientation[4];  // quat xyzw
    float worldScale[4];        // xyz + pad
    float materialOverride[4];
    std::int32_t meshId;
    std::int32_t materialId;
    std::int32_t skeletonId;
    std::int32_t poseRingSlot;
    std::uint32_t flags;
    std::uint32_t sortKeyLow;
    std::uint32_t sortKeyHigh;
    std::uint32_t entityIndex;
    float pad[8];
};

static_assert(sizeof(InstanceLayoutEntry) == 128,
              "InstanceLayoutEntry must be exactly 128 bytes for the "
              "documented std140-friendly layout to hold.");
static_assert(alignof(InstanceLayoutEntry) >= 16,
              "InstanceLayoutEntry must have 16-byte alignment.");

/// Project one @ref DrawItem into the stable layout. Pure function;
/// trivially parallelizable across draw items.
inline InstanceLayoutEntry packInstance(const DrawItem& item) noexcept {
    InstanceLayoutEntry e = {};
    e.worldPosition[0] = item.transform.position.x;
    e.worldPosition[1] = item.transform.position.y;
    e.worldPosition[2] = item.transform.position.z;
    e.worldPosition[3] = 0.0f;
    e.worldOrientation[0] = item.transform.orientation.x;
    e.worldOrientation[1] = item.transform.orientation.y;
    e.worldOrientation[2] = item.transform.orientation.z;
    e.worldOrientation[3] = item.transform.orientation.w;
    e.worldScale[0] = item.transform.scale.x;
    e.worldScale[1] = item.transform.scale.y;
    e.worldScale[2] = item.transform.scale.z;
    e.worldScale[3] = 0.0f;
    std::memcpy(e.materialOverride, item.materialOverride.params.data(),
                sizeof(e.materialOverride));
    e.meshId = item.meshId;
    e.materialId = item.materialId;
    e.skeletonId = item.skeletonId;
    e.poseRingSlot = static_cast<std::int32_t>(item.pose.ringSlot);
    e.flags = item.flags;
    e.sortKeyLow  = static_cast<std::uint32_t>(item.sortKey & 0xFFFFFFFFu);
    e.sortKeyHigh = static_cast<std::uint32_t>((item.sortKey >> 32) & 0xFFFFFFFFu);
    e.entityIndex = item.entity.index;
    return e;
}

/// Pack a span of @ref DrawItem into a destination span of layout
/// entries. Returns the number of entries written, which is
/// `min(items.size(), dst.size())`.
inline std::size_t packInstances(std::span<const DrawItem> items,
                                 std::span<InstanceLayoutEntry> dst) noexcept {
    const std::size_t n = items.size() < dst.size() ? items.size() : dst.size();
    for (std::size_t i = 0; i < n; ++i) {
        dst[i] = packInstance(items[i]);
    }
    return n;
}

} // namespace threadmaxx
