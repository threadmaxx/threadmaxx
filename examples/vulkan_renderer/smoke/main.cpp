// Build-verification smoke for the Vulkan reference renderer (batch 9).
//
// Opens a GLFW window, brings up the engine + Vulkan renderer, registers a
// tiny scene (one camera, a static debug-line cube, a directional light),
// and runs for N ticks (default infinite, Ctrl-C / window close exits).
//
// NOT a demo scene — the RPG demo (batch 10) is the showcase.

#include <threadmaxx_vk/VulkanRenderer.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>
#include <threadmaxx/World.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

constexpr std::uint32_t kWidth  = 1280;
constexpr std::uint32_t kHeight = 720;

/// One spinning third-person camera + a directional light + a static
/// wireframe cube of debug lines. Pushed every tick into the
/// RenderFrameBuilder. Reads/writes empty so it lands wherever in the
/// wave graph.
class SceneSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "smoke-scene"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext& ctx) override { tick_ = ctx.tick(); }
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override {
        const float t = static_cast<float>(tick_) * 0.016f;
        const float radius = 4.0f;
        const float camX = std::sin(t * 0.5f) * radius;
        const float camZ = std::cos(t * 0.5f) * radius;
        const float camY = 2.0f;

        threadmaxx::Camera cam = {};
        cam.id = 0;
        cam.mode = threadmaxx::ProjectionMode::Perspective;
        cam.position = {camX, camY, camZ};
        cam.forward  = {-camX, -camY, -camZ};
        cam.up = {0, 1, 0};
        cam.nearZ = 0.1f;
        cam.farZ = 100.0f;
        cam.fovYRadians = 1.0472f;
        cam.aspect = float(kWidth) / float(kHeight);
        buildView(cam);
        buildPerspective(cam);
        b.addCamera(cam);

        threadmaxx::Light l = {};
        l.type = threadmaxx::LightType::Directional;
        l.direction = {-0.3f, -1.0f, -0.2f};
        l.color = {1.0f, 0.96f, 0.9f};
        l.intensity = 1.0f;
        b.addLight(l);

        // 12-edge wireframe cube around the origin.
        const float h = 0.5f;
        const threadmaxx::Vec3 c[8] = {
            {-h, -h, -h}, { h, -h, -h}, { h,  h, -h}, {-h,  h, -h},
            {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h},
        };
        const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},
            {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7},
        };
        const std::uint32_t color = 0xFF80FFFFu;
        for (auto& e : edges) {
            threadmaxx::DebugLine dl{c[e[0]], c[e[1]], color};
            b.addDebugLine(dl);
        }
    }
private:
    std::uint64_t tick_ = 0;

    static void buildView(threadmaxx::Camera& cam) {
        const threadmaxx::Vec3 eye = cam.position;
        const auto normalize = [](threadmaxx::Vec3 v) {
            const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return n > 0.0f ? threadmaxx::Vec3{v.x / n, v.y / n, v.z / n}
                            : threadmaxx::Vec3{0, 0, 1};
        };
        const auto cross = [](threadmaxx::Vec3 a, threadmaxx::Vec3 b) {
            return threadmaxx::Vec3{a.y * b.z - a.z * b.y,
                                    a.z * b.x - a.x * b.z,
                                    a.x * b.y - a.y * b.x};
        };
        const threadmaxx::Vec3 f = normalize(cam.forward);
        const threadmaxx::Vec3 s = normalize(cross(f, cam.up));
        const threadmaxx::Vec3 u = cross(s, f);
        // Right-handed: -Z forward in camera space (matches GL / our shader's
        // assumption that proj * view * world produces NDC z in [0, 1] after
        // the Vulkan-flipped Y viewport).
        cam.view = {
            s.x,           u.x,           -f.x,          0.0f,
            s.y,           u.y,           -f.y,          0.0f,
            s.z,           u.z,           -f.z,          0.0f,
            -(s.x*eye.x + s.y*eye.y + s.z*eye.z),
            -(u.x*eye.x + u.y*eye.y + u.z*eye.z),
            +(f.x*eye.x + f.y*eye.y + f.z*eye.z),
            1.0f,
        };
    }
    static void buildPerspective(threadmaxx::Camera& cam) {
        const float f = 1.0f / std::tan(cam.fovYRadians * 0.5f);
        const float nf = 1.0f / (cam.nearZ - cam.farZ);
        cam.projection = {
            f / cam.aspect, 0,  0,                                  0,
            0,              f,  0,                                  0,
            0,              0,  (cam.farZ + cam.nearZ) * nf,       -1.0f,
            0,              0,  (2.0f * cam.farZ * cam.nearZ) * nf, 0,
        };
    }
};

void glfwResizeCb(GLFWwindow* win, int width, int height) {
    auto* engine = static_cast<threadmaxx::Engine*>(glfwGetWindowUserPointer(win));
    if (engine && width > 0 && height > 0) {
        engine->notifyResize(static_cast<std::uint32_t>(width),
                             static_cast<std::uint32_t>(height));
    }
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t maxTicks = 0;
    if (argc >= 2) {
        maxTicks = static_cast<std::uint64_t>(std::strtoull(argv[1], nullptr, 10));
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "[smoke] glfwInit failed\n");
        return 1;
    }
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "[smoke] Vulkan loader not available\n");
        glfwTerminate();
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(static_cast<int>(kWidth),
                                          static_cast<int>(kHeight),
                                          "threadmaxx — vulkan smoke",
                                          nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[smoke] glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    threadmaxx::Config cfg;
    cfg.workerCount = 0;
    threadmaxx::Engine engine(cfg);

    struct SmokeGame : threadmaxx::IGame {
        threadmaxx_vk::VulkanRenderer* renderer = nullptr;
        void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                     threadmaxx::CommandBuffer&) override {
            e.registerSystem(std::make_unique<SceneSystem>());
            e.setRenderer(renderer);
        }
    };
    SmokeGame game;

    threadmaxx_vk::VulkanRenderer::Config vrcfg;
    vrcfg.width  = kWidth;
    vrcfg.height = kHeight;
    vrcfg.framesInFlight = 2;
    vrcfg.enableValidation = std::getenv("THREADMAXX_VK_VALIDATE") != nullptr;
    auto renderer = std::make_unique<threadmaxx_vk::VulkanRenderer>(
        &engine, window, vrcfg);
    game.renderer = renderer.get();

    glfwSetWindowUserPointer(window, &engine);
    glfwSetFramebufferSizeCallback(window, glfwResizeCb);

    if (!engine.initialize(game)) {
        std::fprintf(stderr, "[smoke] engine.initialize failed\n");
        return 1;
    }

    std::printf("[smoke] running %s — Ctrl-C / window close to exit\n",
                maxTicks ? "bounded" : "unbounded");

    std::uint64_t tick = 0;
    while (!glfwWindowShouldClose(window) && !engine.quitRequested()) {
        glfwPollEvents();
        engine.step();
        ++tick;
        if (maxTicks && tick >= maxTicks) {
            std::printf("[smoke] reached %llu ticks; exiting\n",
                        static_cast<unsigned long long>(maxTicks));
            break;
        }
    }

    engine.shutdown();

    std::printf("[smoke] %llu frames submitted\n",
                static_cast<unsigned long long>(renderer->framesSubmitted()));

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
