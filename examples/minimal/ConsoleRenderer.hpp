#pragma once

#include <threadmaxx/Renderer.hpp>

#include <cstdint>

// Headless renderer that prints a one-line summary every Nth frame. Stands in
// for a real graphics backend; replacing this with a Vulkan/SDL/OpenGL
// renderer would not require any engine changes.
class ConsoleRenderer : public threadmaxx::IRenderer {
public:
    explicit ConsoleRenderer(std::uint32_t printEvery = 60) : printEvery_(printEvery) {}

    bool initialize() override;
    void shutdown() override;
    void submitFrame(const threadmaxx::RenderFrame& frame) override;

private:
    std::uint32_t printEvery_;
    std::uint64_t framesSubmitted_ = 0;
};
