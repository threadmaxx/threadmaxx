#include "ConsoleRenderer.hpp"

#include <cstdio>

bool ConsoleRenderer::initialize() {
    std::printf("[ConsoleRenderer] initialized\n");
    return true;
}

void ConsoleRenderer::shutdown() {
    std::printf("[ConsoleRenderer] shutdown after %llu frames\n",
                static_cast<unsigned long long>(framesSubmitted_));
}

void ConsoleRenderer::submitFrame(const threadmaxx::RenderFrame& frame) {
    framesSubmitted_++;
    if (printEvery_ == 0 || (framesSubmitted_ % printEvery_) != 0) return;

    // Average position of the first few instances, just to prove the data
    // got here.
    float avgX = 0.0f, avgZ = 0.0f;
    const auto n = frame.instances.size();
    for (const auto& inst : frame.instances) {
        avgX += inst.transform.position.x;
        avgZ += inst.transform.position.z;
    }
    if (n > 0) {
        avgX /= static_cast<float>(n);
        avgZ /= static_cast<float>(n);
    }

    std::printf("[frame] tick=%llu  t=%.2fs  instances=%zu  avgXZ=(%.2f, %.2f)\n",
                static_cast<unsigned long long>(frame.tick),
                frame.simulationTime, n, avgX, avgZ);
}
