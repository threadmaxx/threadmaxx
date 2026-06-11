#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../types.hpp"

namespace threadmaxx::assets {

struct Aabb {
    float min[3]{};
    float max[3]{};
};

struct MeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(MeshVertex) == 32, "MeshVertex layout pinned");

struct MeshSubmesh {
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::uint32_t materialIndex{kInvalidAssetId};
    std::string   materialName;
};

struct MeshData {
    std::vector<MeshVertex>     vertices;
    std::vector<std::uint32_t>  indices;
    std::vector<MeshSubmesh>    submeshes;
    Aabb                        aabb;
    std::string                 sourcePath;
};

} // namespace threadmaxx::assets
