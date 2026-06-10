// `threadmaxx_navmesh` N6 — batch path solver throughput.
//
// Builds a flat NxN polygon grid in-process (no test-fixture dep) and
// runs `BatchPathSolver::solve` over a deterministic 1k-request batch.
// Prints queries/sec for a few worker counts so we can eyeball the
// scaling curve. Not registered with CTest; opt-in via
// `-DTHREADMAXX_BUILD_BENCHMARKS=ON`.

#include "threadmaxx_navmesh/config.hpp"
#include "threadmaxx_navmesh/crowd.hpp"
#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/query.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

using ::threadmaxx::Vec3;
using ::threadmaxx::navmesh::BatchPathRequest;
using ::threadmaxx::navmesh::BatchPathResult;
using ::threadmaxx::navmesh::BatchPathSolver;
using ::threadmaxx::navmesh::kInvalidPolyIndex;
using ::threadmaxx::navmesh::kNavMeshBlobMagic;
using ::threadmaxx::navmesh::kNavMeshBlobVersion;
using ::threadmaxx::navmesh::NavMeshRef;
using ::threadmaxx::navmesh::NavMeshRegistry;
using ::threadmaxx::navmesh::NavPoly;
using ::threadmaxx::navmesh::PathRequest;

// Tiny blob writer that mirrors `tests/navmesh/fixtures/blob_builder.hpp`
// without dragging the test header in.
class BlobBuilder {
public:
    template <typename T>
    void writePod(const T& v) {
        const auto offset = bytes_.size();
        bytes_.resize(offset + sizeof(T));
        std::memcpy(bytes_.data() + offset, &v, sizeof(T));
    }
    void writeU32(std::uint32_t v) { writePod(v); }
    void writeString(const std::string& s) {
        writeU32(static_cast<std::uint32_t>(s.size()));
        const auto offset = bytes_.size();
        bytes_.resize(offset + s.size());
        std::memcpy(bytes_.data() + offset, s.data(), s.size());
    }
    template <typename T>
    void writeVec(const std::vector<T>& v) {
        const std::size_t b = v.size() * sizeof(T);
        const auto offset = bytes_.size();
        bytes_.resize(offset + b);
        if (b) std::memcpy(bytes_.data() + offset, v.data(), b);
    }
    std::span<const std::byte> view() const noexcept {
        return std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes_.data()), bytes_.size());
    }
private:
    std::vector<unsigned char> bytes_;
};

/// Build an NxN-quad flat grid as a single tile. `N=16` → 256 polys.
std::vector<unsigned char> makeFlatGrid(int n) {
    BlobBuilder b;
    b.writeU32(kNavMeshBlobMagic);
    b.writeU32(kNavMeshBlobVersion);
    b.writeString("flat_grid_bench");
    b.writeU32(1);

    const std::uint32_t tileId = 0;
    const int verts_per_row = n + 1;
    std::vector<Vec3> verts;
    verts.reserve(static_cast<std::size_t>(verts_per_row * verts_per_row));
    for (int z = 0; z < verts_per_row; ++z) {
        for (int x = 0; x < verts_per_row; ++x) {
            verts.push_back(Vec3{static_cast<float>(x), 0.0f,
                                 static_cast<float>(z)});
        }
    }
    std::vector<NavPoly> polys;
    std::vector<std::uint32_t> idx;
    std::vector<std::uint32_t> neighbors;
    polys.reserve(static_cast<std::size_t>(n * n));
    idx.reserve(static_cast<std::size_t>(n * n * 4));
    neighbors.reserve(static_cast<std::size_t>(n * n * 4));
    auto vid = [verts_per_row](int x, int z) {
        return static_cast<std::uint32_t>(z * verts_per_row + x);
    };
    auto polyAt = [n](int xx, int zz) {
        if (xx < 0 || xx >= n || zz < 0 || zz >= n)
            return kInvalidPolyIndex;
        return static_cast<std::uint32_t>(zz * n + xx);
    };
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            NavPoly p;
            p.indexStart = static_cast<std::uint32_t>(idx.size());
            p.indexCount = 4;
            p.areaTag = 0;
            polys.push_back(p);
            idx.push_back(vid(x,     z    ));
            idx.push_back(vid(x + 1, z    ));
            idx.push_back(vid(x + 1, z + 1));
            idx.push_back(vid(x,     z + 1));
            neighbors.push_back(polyAt(x,     z - 1));
            neighbors.push_back(polyAt(x + 1, z    ));
            neighbors.push_back(polyAt(x,     z + 1));
            neighbors.push_back(polyAt(x - 1, z    ));
        }
    }

    b.writeU32(tileId);
    b.writeU32(static_cast<std::uint32_t>(verts.size()));
    b.writeU32(static_cast<std::uint32_t>(polys.size()));
    b.writeU32(static_cast<std::uint32_t>(idx.size()));
    b.writeVec(verts);
    b.writeVec(polys);
    b.writeVec(idx);
    b.writeVec(neighbors);
    b.writeU32(0); // no portals

    const auto v = b.view();
    return std::vector<unsigned char>(
        reinterpret_cast<const unsigned char*>(v.data()),
        reinterpret_cast<const unsigned char*>(v.data()) + v.size());
}

double runOnce(NavMeshRegistry& reg, NavMeshRef ref,
               std::uint32_t workers, std::size_t requestCount,
               int gridSize) {
    BatchPathRequest batch;
    batch.requests.reserve(requestCount);
    const float maxCoord = static_cast<float>(gridSize) - 0.5f;
    for (std::size_t i = 0; i < requestCount; ++i) {
        PathRequest r;
        r.mesh = ref;
        // Pseudo-random but deterministic (start, goal) pairs spread
        // across the grid. Bit-mixing avoids alignment with the grid
        // tiling that would make every solve degenerate to one cell.
        const std::uint64_t s = i * 1469598103934665603ull;
        const float sx = 0.5f + std::fmod(
            static_cast<float>(s % 1000ull) / 1000.0f * maxCoord, maxCoord);
        const float sz = 0.5f + std::fmod(
            static_cast<float>((s >> 16) % 1000ull) / 1000.0f * maxCoord,
            maxCoord);
        const float gx = 0.5f + std::fmod(
            static_cast<float>((s >> 32) % 1000ull) / 1000.0f * maxCoord,
            maxCoord);
        const float gz = 0.5f + std::fmod(
            static_cast<float>((s >> 48) % 1000ull) / 1000.0f * maxCoord,
            maxCoord);
        r.start = Vec3{sx, 0.0f, sz};
        r.goal  = Vec3{gx, 0.0f, gz};
        r.allowPartial = true;
        batch.requests.push_back(r);
    }

    BatchPathSolver solver(reg, BatchPathSolver::Config{workers});

    // Warmup pass so worker threads are scheduled by the time we start
    // the clock.
    (void)solver.solve(batch);

    const auto t0 = std::chrono::steady_clock::now();
    BatchPathResult out = solver.solve(batch);
    const auto t1 = std::chrono::steady_clock::now();

    std::size_t successCount = 0;
    for (const auto& r : out.results) {
        if (r.success) ++successCount;
    }
    const double seconds =
        std::chrono::duration<double>(t1 - t0).count();
    const double qps = static_cast<double>(requestCount) / seconds;
    std::printf("workers=%u  requests=%zu  success=%zu  elapsed=%.4fs  qps=%.1f\n",
                workers, requestCount, successCount, seconds, qps);
    return qps;
}

} // namespace

int main(int argc, char** argv) {
    int gridSize = 16; // 16x16 = 256 polys
    std::size_t requestCount = 1000;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--grid=", 0) == 0) {
            gridSize = std::stoi(a.substr(7));
        } else if (a.rfind("--requests=", 0) == 0) {
            requestCount = static_cast<std::size_t>(std::stoul(a.substr(11)));
        }
    }

    auto blob = makeFlatGrid(gridSize);
    NavMeshRegistry reg;
    NavMeshRef ref = reg.load(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(blob.data()), blob.size()));
    if (!reg.isValid(ref)) {
        std::fprintf(stderr, "bench: failed to load synthesized navmesh\n");
        return 1;
    }
    std::printf("threadmaxx_navmesh batch bench: %dx%d grid (%d polys)\n",
                gridSize, gridSize, gridSize * gridSize);

    for (std::uint32_t w : {std::uint32_t{0}, std::uint32_t{1},
                            std::uint32_t{2}, std::uint32_t{4},
                            std::uint32_t{8}}) {
        runOnce(reg, ref, w, requestCount, gridSize);
    }

    return 0;
}
