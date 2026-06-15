#include "MeshLoader.hpp"

#include "VkUtil.hpp"

#include <threadmaxx/Engine.hpp>

#include <array>
#include <cstring>

namespace threadmaxx_vk {

namespace {

// Unit cube centered at the origin, half-extent 0.5. 24 vertices (4 per
// face) so each face carries its own normal. Position[3] + Normal[3].
constexpr std::array<float, 24 * 6> kCubeVertices = {
    -0.5f, -0.5f, -0.5f,  -1, 0, 0,
    -0.5f, -0.5f,  0.5f,  -1, 0, 0,
    -0.5f,  0.5f,  0.5f,  -1, 0, 0,
    -0.5f,  0.5f, -0.5f,  -1, 0, 0,
     0.5f, -0.5f,  0.5f,   1, 0, 0,
     0.5f, -0.5f, -0.5f,   1, 0, 0,
     0.5f,  0.5f, -0.5f,   1, 0, 0,
     0.5f,  0.5f,  0.5f,   1, 0, 0,
    -0.5f, -0.5f, -0.5f,  0, -1, 0,
     0.5f, -0.5f, -0.5f,  0, -1, 0,
     0.5f, -0.5f,  0.5f,  0, -1, 0,
    -0.5f, -0.5f,  0.5f,  0, -1, 0,
    -0.5f,  0.5f,  0.5f,  0, 1, 0,
     0.5f,  0.5f,  0.5f,  0, 1, 0,
     0.5f,  0.5f, -0.5f,  0, 1, 0,
    -0.5f,  0.5f, -0.5f,  0, 1, 0,
     0.5f, -0.5f, -0.5f,  0, 0, -1,
    -0.5f, -0.5f, -0.5f,  0, 0, -1,
    -0.5f,  0.5f, -0.5f,  0, 0, -1,
     0.5f,  0.5f, -0.5f,  0, 0, -1,
    -0.5f, -0.5f,  0.5f,  0, 0, 1,
     0.5f, -0.5f,  0.5f,  0, 0, 1,
     0.5f,  0.5f,  0.5f,  0, 0, 1,
    -0.5f,  0.5f,  0.5f,  0, 0, 1,
};

constexpr std::array<std::uint16_t, 36> kCubeIndices = {
    0, 1, 2, 0, 2, 3,
    4, 5, 6, 4, 6, 7,
    8, 9,10, 8,10,11,
   12,13,14,12,14,15,
   16,17,18,16,18,19,
   20,21,22,20,22,23,
};

void createBuffer(VulkanContext& ctx, VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(ctx.device(), &bi, nullptr, &buffer));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device(), buffer, &mr);
    VkMemoryAllocateInfo ma = {};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits, properties);
    VK_CHECK(vkAllocateMemory(ctx.device(), &ma, nullptr, &memory));
    VK_CHECK(vkBindBufferMemory(ctx.device(), buffer, memory, 0));
}

} // namespace

void MeshLoader::onShutdown(threadmaxx::Engine& /*engine*/) {
    // GPU resources have already been freed by
    // @ref VulkanRenderer::shutdown via @ref releaseGpuResources; here
    // we just drop the registry handle so the slot's refcount goes to
    // zero before the registry is destroyed.
    unitCube_.reset();
}

void MeshLoader::releaseGpuResources() noexcept {
    if (!ctx_.device()) return;
    for (auto& m : ownedMeshes_) {
        if (m.vertexBuffer) vkDestroyBuffer(ctx_.device(), m.vertexBuffer, nullptr);
        if (m.vertexMemory) vkFreeMemory  (ctx_.device(), m.vertexMemory, nullptr);
        if (m.indexBuffer)  vkDestroyBuffer(ctx_.device(), m.indexBuffer, nullptr);
        if (m.indexMemory)  vkFreeMemory  (ctx_.device(), m.indexMemory, nullptr);
    }
    ownedMeshes_.clear();
    resident_.store(0, std::memory_order_relaxed);
}

threadmaxx::LoaderStats MeshLoader::stats() const noexcept {
    threadmaxx::LoaderStats s;
    s.memoryFootprint = resident_.load(std::memory_order_relaxed);
    return s;
}

threadmaxx::ResourceHandle<Mesh> MeshLoader::createUnitCube(threadmaxx::Engine& engine) {
    auto handle = createMesh(
        engine,
        std::span<const float>(kCubeVertices.data(), kCubeVertices.size()),
        std::span<const std::uint16_t>(kCubeIndices.data(), kCubeIndices.size()));
    unitCube_ = handle;
    return handle;
}

// §3.11 batch 9b.2 — generic upload. Used by both `createUnitCube`
// (legacy procedural cube) and the demo's runtime OBJ-loaded meshes.
// The opaque pipeline binds binding 0 with a 24-byte stride (3 pos +
// 3 normal floats per corner); the function asserts that the input
// matches.
threadmaxx::ResourceHandle<Mesh> MeshLoader::createMesh(
    threadmaxx::Engine&             engine,
    std::span<const float>          vertices,
    std::span<const std::uint16_t>  indices,
    std::uint32_t                   vertexStrideFloats) {

    if (vertices.empty() || indices.empty()) {
        return threadmaxx::ResourceHandle<Mesh>{};
    }
    if (vertexStrideFloats == 0u
        || (vertices.size() % vertexStrideFloats) != 0u) {
        return threadmaxx::ResourceHandle<Mesh>{};
    }
    if ((indices.size() % 3u) != 0u) {
        return threadmaxx::ResourceHandle<Mesh>{};
    }

    Mesh m;
    const VkDeviceSize vBytes = vertices.size_bytes();
    const VkDeviceSize iBytes = indices.size_bytes();

    createBuffer(ctx_, vBytes,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m.vertexBuffer, m.vertexMemory);
    void* p = nullptr;
    VK_CHECK(vkMapMemory(ctx_.device(), m.vertexMemory, 0, vBytes, 0, &p));
    std::memcpy(p, vertices.data(), vBytes);
    vkUnmapMemory(ctx_.device(), m.vertexMemory);

    createBuffer(ctx_, iBytes,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m.indexBuffer, m.indexMemory);
    VK_CHECK(vkMapMemory(ctx_.device(), m.indexMemory, 0, iBytes, 0, &p));
    std::memcpy(p, indices.data(), iBytes);
    vkUnmapMemory(ctx_.device(), m.indexMemory);

    m.indexCount = static_cast<std::uint32_t>(indices.size());
    m.gpuReady   = true;

    ownedMeshes_.push_back(m);
    resident_.fetch_add(vBytes + iBytes, std::memory_order_relaxed);

    return engine.resources().addRefCounted<Mesh>(m);
}

} // namespace threadmaxx_vk
