#include "threadmaxx_navmesh/mesh.hpp"

#include "threadmaxx_navmesh/config.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace threadmaxx::navmesh {

namespace {

/// Cursor over the input blob — bounds-checked reads.
class Cursor {
public:
    Cursor(std::span<const std::byte> data) noexcept
        : ptr_(data.data()), end_(data.data() + data.size()) {}

    bool readU32(std::uint32_t& out) noexcept {
        return readPod(out);
    }

    bool readU64(std::uint64_t& out) noexcept {
        return readPod(out);
    }

    bool readF32(float& out) noexcept {
        return readPod(out);
    }

    bool readString(std::string& out) noexcept {
        std::uint32_t len = 0;
        if (!readU32(len)) return false;
        if (len > 4096) return false;  // sanity cap
        if (ptr_ + len > end_) return false;
        out.assign(reinterpret_cast<const char*>(ptr_), len);
        ptr_ += len;
        return true;
    }

    template <typename T>
    bool readVec(std::vector<T>& out, std::uint32_t count) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (ptr_ + static_cast<std::size_t>(count) * sizeof(T) > end_) return false;
        out.resize(count);
        std::memcpy(out.data(), ptr_, static_cast<std::size_t>(count) * sizeof(T));
        ptr_ += static_cast<std::size_t>(count) * sizeof(T);
        return true;
    }

    bool atEnd() const noexcept { return ptr_ == end_; }

private:
    template <typename T>
    bool readPod(T& out) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (ptr_ + sizeof(T) > end_) return false;
        std::memcpy(&out, ptr_, sizeof(T));
        ptr_ += sizeof(T);
        return true;
    }

    const std::byte* ptr_;
    const std::byte* end_;
};

/// Refresh `aabbMin` / `aabbMax` from `tile.vertices`. Empty tile
/// leaves a zero-extent box centered at the origin.
void recomputeAabb(NavTile& tile) noexcept {
    if (tile.vertices.empty()) {
        tile.aabbMin = Vec3{};
        tile.aabbMax = Vec3{};
        return;
    }
    Vec3 lo = tile.vertices.front();
    Vec3 hi = lo;
    for (const Vec3& v : tile.vertices) {
        lo.x = std::min(lo.x, v.x);
        lo.y = std::min(lo.y, v.y);
        lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x);
        hi.y = std::max(hi.y, v.y);
        hi.z = std::max(hi.z, v.z);
    }
    tile.aabbMin = lo;
    tile.aabbMax = hi;
}

} // namespace

// -- NavMesh -----------------------------------------------------------------

const NavTile* NavMesh::findTile(NavTileId id) const noexcept {
    for (const NavTile& t : tiles_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::optional<std::size_t> NavMesh::tileIndex(NavTileId id) const noexcept {
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        if (tiles_[i].id == id) return i;
    }
    return std::nullopt;
}

std::span<const std::uint32_t> NavMesh::portalsForTile(NavTileId id) const noexcept {
    const auto idx = tileIndex(id);
    if (!idx) return {};
    if (*idx >= portalsByTile_.size()) return {};
    return portalsByTile_[*idx];
}

std::optional<NavMesh::CrossTileNeighbor> NavMesh::crossTileNeighbor(
    NavTileId tileId, NavPolyId polyId, std::uint32_t edgeIdx) const noexcept {

    const auto idx = tileIndex(tileId);
    if (!idx) return std::nullopt;
    if (*idx >= portalsByTile_.size()) return std::nullopt;
    for (std::uint32_t pi : portalsByTile_[*idx]) {
        const NavPortal& p = portals_[pi];
        if (p.tileA == tileId && p.polyA == polyId && p.edgeA == edgeIdx) {
            return CrossTileNeighbor{p.tileB, p.polyB, p.edgeB};
        }
        if (p.tileB == tileId && p.polyB == polyId && p.edgeB == edgeIdx) {
            return CrossTileNeighbor{p.tileA, p.polyA, p.edgeA};
        }
    }
    return std::nullopt;
}

// -- NavMeshRegistry::Impl ---------------------------------------------------

struct NavMeshRegistry::Impl {
    mutable std::mutex mtx;
    std::vector<Slot> slots;          // index 0 reserved as "invalid"
    std::vector<std::size_t> free;    // recycled slot indices

    Impl() {
        // Slot 0 is the sentinel — keep it perpetually empty.
        slots.emplace_back();
    }
};

// -- NavMeshRegistry ---------------------------------------------------------

NavMeshRegistry::NavMeshRegistry() : impl_(std::make_unique<Impl>()) {}
NavMeshRegistry::~NavMeshRegistry() = default;
NavMeshRegistry::NavMeshRegistry(NavMeshRegistry&&) noexcept = default;
NavMeshRegistry& NavMeshRegistry::operator=(NavMeshRegistry&&) noexcept = default;

NavMeshRef NavMeshRegistry::load(std::span<const std::byte> bakedData) {
    lastError_ = NavMeshLoadError::None;

    if (bakedData.empty()) {
        lastError_ = NavMeshLoadError::EmptyBlob;
        return {};
    }

    Cursor c{bakedData};

    std::uint32_t magic = 0;
    if (!c.readU32(magic)) { lastError_ = NavMeshLoadError::Truncated; return {}; }
    if (magic != kNavMeshBlobMagic) {
        lastError_ = NavMeshLoadError::InvalidMagic;
        return {};
    }

    std::uint32_t version = 0;
    if (!c.readU32(version)) { lastError_ = NavMeshLoadError::Truncated; return {}; }
    if (version != kNavMeshBlobVersion) {
        lastError_ = NavMeshLoadError::UnsupportedVersion;
        return {};
    }

    auto mesh = std::make_unique<NavMesh>();
    if (!c.readString(mesh->name_)) {
        lastError_ = NavMeshLoadError::Truncated;
        return {};
    }

    std::uint32_t tileCount = 0;
    if (!c.readU32(tileCount)) { lastError_ = NavMeshLoadError::Truncated; return {}; }
    if (tileCount > kNavMeshMaxTiles) {
        lastError_ = NavMeshLoadError::InvalidTileCount;
        return {};
    }

    mesh->tiles_.reserve(tileCount);
    std::uint32_t polyTotal = 0;
    std::uint32_t vertTotal = 0;

    for (std::uint32_t i = 0; i < tileCount; ++i) {
        NavTile tile;
        std::uint32_t id = 0;
        std::uint32_t vertexCount = 0;
        std::uint32_t polyCount = 0;
        std::uint32_t indexCount = 0;
        if (!c.readU32(id) ||
            !c.readU32(vertexCount) ||
            !c.readU32(polyCount) ||
            !c.readU32(indexCount)) {
            lastError_ = NavMeshLoadError::Truncated;
            return {};
        }
        if (polyCount > kNavMeshMaxPolysPerTile) {
            lastError_ = NavMeshLoadError::InvalidPolyCount;
            return {};
        }
        tile.id = id;
        if (!c.readVec(tile.vertices, vertexCount)) {
            lastError_ = NavMeshLoadError::Truncated;
            return {};
        }
        if (!c.readVec(tile.polygons, polyCount)) {
            lastError_ = NavMeshLoadError::Truncated;
            return {};
        }
        if (!c.readVec(tile.vertexIndices, indexCount)) {
            lastError_ = NavMeshLoadError::Truncated;
            return {};
        }
        if (!c.readVec(tile.neighborPolys, indexCount)) {
            lastError_ = NavMeshLoadError::Truncated;
            return {};
        }
        // Validate every polygon's index slice fits inside the pools.
        for (const NavPoly& p : tile.polygons) {
            const std::uint64_t end =
                static_cast<std::uint64_t>(p.indexStart) +
                static_cast<std::uint64_t>(p.indexCount);
            if (end > tile.vertexIndices.size()) {
                lastError_ = NavMeshLoadError::InvalidIndex;
                return {};
            }
            for (std::uint16_t k = 0; k < p.indexCount; ++k) {
                const std::uint32_t vi = tile.vertexIndices[p.indexStart + k];
                if (vi >= tile.vertices.size()) {
                    lastError_ = NavMeshLoadError::InvalidIndex;
                    return {};
                }
            }
        }
        recomputeAabb(tile);
        polyTotal += polyCount;
        vertTotal += vertexCount;
        mesh->tiles_.push_back(std::move(tile));
    }

    mesh->polygonCount_ = polyTotal;
    mesh->vertexCount_ = vertTotal;

    // -- v2: cross-tile portal table -----------------------------------------
    std::uint32_t portalCount = 0;
    if (!c.readU32(portalCount)) {
        lastError_ = NavMeshLoadError::Truncated;
        return {};
    }
    if (portalCount > kNavMeshMaxPortals) {
        lastError_ = NavMeshLoadError::InvalidPortalCount;
        return {};
    }
    if (!c.readVec(mesh->portals_, portalCount)) {
        lastError_ = NavMeshLoadError::Truncated;
        return {};
    }
    // Per-portal validation: tiles exist + differ, polys + edges in range.
    mesh->portalsByTile_.assign(mesh->tiles_.size(), {});
    for (std::uint32_t pi = 0; pi < portalCount; ++pi) {
        const NavPortal& p = mesh->portals_[pi];
        if (p.tileA == p.tileB) {
            lastError_ = NavMeshLoadError::InvalidPortal;
            return {};
        }
        const auto idxA = mesh->tileIndex(p.tileA);
        const auto idxB = mesh->tileIndex(p.tileB);
        if (!idxA || !idxB) {
            lastError_ = NavMeshLoadError::InvalidPortal;
            return {};
        }
        const NavTile& tA = mesh->tiles_[*idxA];
        const NavTile& tB = mesh->tiles_[*idxB];
        if (p.polyA >= tA.polygons.size() || p.polyB >= tB.polygons.size()) {
            lastError_ = NavMeshLoadError::InvalidPortal;
            return {};
        }
        const std::uint32_t edgesA = tA.polygons[p.polyA].indexCount;
        const std::uint32_t edgesB = tB.polygons[p.polyB].indexCount;
        if (p.edgeA >= edgesA || p.edgeB >= edgesB) {
            lastError_ = NavMeshLoadError::InvalidPortal;
            return {};
        }
        mesh->portalsByTile_[*idxA].push_back(pi);
        mesh->portalsByTile_[*idxB].push_back(pi);
    }

    std::lock_guard<std::mutex> g(impl_->mtx);
    std::size_t slotIdx;
    if (!impl_->free.empty()) {
        slotIdx = impl_->free.back();
        impl_->free.pop_back();
    } else {
        slotIdx = impl_->slots.size();
        impl_->slots.emplace_back();
    }
    Slot& s = impl_->slots[slotIdx];
    ++s.generation;
    s.mesh = std::move(mesh);

    NavMeshRef ref;
    ref.id = static_cast<NavMeshId>(slotIdx);
    ref.generation = s.generation;
    return ref;
}

void NavMeshRegistry::unload(NavMeshRef ref) {
    if (!ref) return;
    std::lock_guard<std::mutex> g(impl_->mtx);
    if (ref.id >= impl_->slots.size()) return;
    Slot& s = impl_->slots[static_cast<std::size_t>(ref.id)];
    if (s.generation != ref.generation || !s.mesh) return;
    s.mesh.reset();
    impl_->free.push_back(static_cast<std::size_t>(ref.id));
}

bool NavMeshRegistry::isValid(NavMeshRef ref) const noexcept {
    if (!ref) return false;
    std::lock_guard<std::mutex> g(impl_->mtx);
    if (ref.id >= impl_->slots.size()) return false;
    const Slot& s = impl_->slots[static_cast<std::size_t>(ref.id)];
    return s.generation == ref.generation && s.mesh != nullptr;
}

std::optional<NavMeshMeta> NavMeshRegistry::meta(NavMeshRef ref) const {
    if (!ref) return std::nullopt;
    std::lock_guard<std::mutex> g(impl_->mtx);
    if (ref.id >= impl_->slots.size()) return std::nullopt;
    const Slot& s = impl_->slots[static_cast<std::size_t>(ref.id)];
    if (s.generation != ref.generation || !s.mesh) return std::nullopt;
    NavMeshMeta m;
    m.name = s.mesh->name_;
    m.tileCount = static_cast<std::uint32_t>(s.mesh->tiles_.size());
    m.polygonCount = s.mesh->polygonCount_;
    m.vertexCount = s.mesh->vertexCount_;
    return m;
}

const NavMesh* NavMeshRegistry::find(NavMeshRef ref) const noexcept {
    if (!ref) return nullptr;
    std::lock_guard<std::mutex> g(impl_->mtx);
    if (ref.id >= impl_->slots.size()) return nullptr;
    const Slot& s = impl_->slots[static_cast<std::size_t>(ref.id)];
    if (s.generation != ref.generation) return nullptr;
    return s.mesh.get();
}

std::size_t NavMeshRegistry::size() const noexcept {
    std::lock_guard<std::mutex> g(impl_->mtx);
    std::size_t count = 0;
    for (const Slot& s : impl_->slots) {
        if (s.mesh) ++count;
    }
    return count;
}

} // namespace threadmaxx::navmesh
