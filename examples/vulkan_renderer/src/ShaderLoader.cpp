#include "ShaderLoader.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>

#include <cstdio>
#include <fstream>
#include <typeindex>

namespace threadmaxx_vk {

namespace {

bool readBinary(const std::filesystem::path& path,
                std::vector<std::uint32_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto bytes = f.tellg();
    if (bytes < 0 || (static_cast<std::size_t>(bytes) % 4) != 0) return false;
    out.assign(static_cast<std::size_t>(bytes) / 4, 0u);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), bytes);
    return f.good();
}

} // namespace

void ShaderLoader::update(threadmaxx::Engine& engine) {
    std::vector<threadmaxx::ResourceId<Shader>> reloads;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        reloads.swap(pendingReloads_);
        pendingCount_.store(0, std::memory_order_relaxed);
    }
    if (reloads.empty()) return;

    auto& ch = engine.events<threadmaxx::AssetReloaded>();

    for (const auto& oldId : reloads) {
        std::filesystem::path path;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (const auto& e : entries_) {
                if (e.id == oldId) { path = e.path; break; }
            }
        }
        if (path.empty()) continue;

        Shader replacement;
        if (!readBinary(path, replacement.spirv)) {
            std::fprintf(stderr,
                "[shader_loader] reload failed: %s\n", path.string().c_str());
            continue;
        }

        const auto newId = engine.resources().add<Shader>(std::move(replacement));
        threadmaxx::AssetReloaded ev{
            oldId.index, oldId.generation, newId.index, newId.generation,
            std::type_index(typeid(Shader)),
        };
        ch.emit(ev);

        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& e : entries_) {
            if (e.id == oldId) {
                e.id = newId;
                break;
            }
        }
    }
}

void ShaderLoader::onShutdown(threadmaxx::Engine& /*engine*/) {
    std::lock_guard<std::mutex> lock(mtx_);
    entries_.clear();
    pendingReloads_.clear();
    pendingCount_.store(0, std::memory_order_relaxed);
    resident_.store(0, std::memory_order_relaxed);
}

threadmaxx::LoaderStats ShaderLoader::stats() const noexcept {
    threadmaxx::LoaderStats s;
    s.memoryFootprint = resident_.load(std::memory_order_relaxed);
    s.pendingLoads = pendingCount_.load(std::memory_order_relaxed);
    return s;
}

void ShaderLoader::markStale(std::uint32_t index,
                             std::uint32_t generation,
                             std::type_index type) {
    if (type != std::type_index(typeid(Shader))) return;
    queueReload_({index, generation});
}

void ShaderLoader::queueReload_(threadmaxx::ResourceId<Shader> id) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& existing : pendingReloads_) {
        if (existing == id) return;
    }
    pendingReloads_.push_back(id);
    pendingCount_.store(pendingReloads_.size(), std::memory_order_relaxed);
}

threadmaxx::ResourceHandle<Shader> ShaderLoader::add(
    threadmaxx::Engine& engine,
    std::filesystem::path path,
    std::span<const std::uint32_t> spirv) {

    Shader s;
    s.spirv.assign(spirv.begin(), spirv.end());
    resident_.fetch_add(spirv.size_bytes(), std::memory_order_relaxed);

    auto handle = engine.resources().addRefCounted<Shader>(std::move(s));

    std::lock_guard<std::mutex> lock(mtx_);
    entries_.push_back(Entry{handle.id(), std::move(path)});
    return handle;
}

} // namespace threadmaxx_vk
