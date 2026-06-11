// Headless walkthrough that exercises every threadmaxx_input pillar over
// 600 frames. Prints summary stats and exits 0 on success.

#include <cstdio>
#include <cstring>

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/binding.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/picking.hpp"
#include "threadmaxx_input/trace.hpp"

namespace {

void identity4(float m[16]) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

}  // namespace

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    BindingSet bs;
    bs.bind("Jump", Binding::key(Key::Space));
    bs.bind("Jump", Binding::gamepadButton(GamepadButton::A));
    bs.bind("Save", Binding::key(Key::S, Modifiers::Ctrl));
    bs.bind("Forward", Binding::gamepadAxisPositive(GamepadAxis::LStickY, 0.3f));
    ctx.setBindings(bs);

    InputTrace trace;
    trace.reserve(600);

    int jumpFires = 0;
    int saveFires = 0;
    int forwardActiveFrames = 0;

    auto stim = [&](int frame) {
        const int phase = frame % 60;
        if (phase == 0) backend.push(KeyEvent{Key::Space, Modifiers::None, true});
        if (phase == 5) backend.push(KeyEvent{Key::Space, Modifiers::None, false});
        if (phase == 30) backend.push(KeyEvent{Key::LCtrl, Modifiers::Ctrl, true});
        if (phase == 32) backend.push(KeyEvent{Key::S, Modifiers::Ctrl, true});
        if (phase == 35) backend.push(KeyEvent{Key::S, Modifiers::Ctrl, false});
        if (phase == 37) backend.push(KeyEvent{Key::LCtrl, Modifiers::None, false});
        if (phase == 40) backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.8f});
        if (phase == 55) backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.0f});
    };

    for (int frame = 0; frame < 600; ++frame) {
        stim(frame);
        ctx.beginFrame(1.0f / 60.0f);
        trace.recordCurrentFrame(ctx);

        if (ctx.action("Jump").pressed) ++jumpFires;
        if (ctx.action("Save").pressed) ++saveFires;
        if (ctx.action("Forward").held) ++forwardActiveFrames;

        ctx.endFrame();
    }

    // Picking sanity — identity camera + screen center yields (0, 0, 0) ray
    // origin and (0, 0, 1) ray direction.
    Camera cam{};
    identity4(cam.view);
    identity4(cam.projection);
    cam.viewportW = 1280.0f;
    cam.viewportH = 720.0f;
    const Ray r = screenToRay(cam, 640.0f, 360.0f);

    // Round-trip trace.
    const auto bytes = trace.serialize();
    InputTrace replay;
    if (!replay.deserialize(bytes)) {
        std::fprintf(stderr, "trace round-trip failed\n");
        return 1;
    }

    std::printf("[input_demo] ran 600 frames\n");
    std::printf("            jump_pressed=%d save_pressed=%d forward_held_frames=%d\n",
                jumpFires, saveFires, forwardActiveFrames);
    std::printf("            picking_dir=(%.2f, %.2f, %.2f)\n",
                r.direction[0], r.direction[1], r.direction[2]);
    std::printf("            trace bytes=%zu replay_frames=%llu\n",
                bytes.size(),
                static_cast<unsigned long long>(replay.frameCount()));
    std::printf("[input_demo] OK\n");
    return 0;
}
