#pragma once

#include "RenderFrame.hpp"

#include <cstdint>

namespace threadmaxx {

/// Renderer-side interface. The engine owns scheduling and snapshotting;
/// the renderer is told "here is the frame to draw".
///
/// A null renderer is allowed (headless mode) and the engine simply
/// skips `submitFrame()` calls.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// Called once when the engine starts. A return of `false` aborts
    /// `Engine::initialize`; the engine treats the renderer as if it
    /// were never installed and `shutdown()` is NOT called on a
    /// renderer whose `initialize()` returned false (the engine assumes
    /// the renderer has already cleaned up internally before reporting
    /// failure).
    /// @return false to abort startup.
    virtual bool initialize() { return true; }

    /// Called once at shutdown after the final frame has been
    /// submitted. Skipped entirely if `initialize()` returned false or
    /// if the renderer was never installed (`Engine::setRenderer(nullptr)`).
    virtual void shutdown() {}

    /// §3.6.5 batch 15a — informational hook fired in response to
    /// `Engine::notifyResize(w, h)`. Default no-op; renderers bound to
    /// a swapchain typically rebuild it here. The engine never
    /// independently watches the host window — game code is expected
    /// to forward platform resize events into `notifyResize`.
    ///
    /// Called on the simulation thread, between `step()` invocations.
    /// The renderer must not retain pointers from the previously-
    /// submitted @ref RenderFrame across this call.
    virtual void onResize(std::uint32_t /*width*/,
                          std::uint32_t /*height*/) {}

    /// Called from the simulation thread after each commit, with the
    /// engine-owned @ref RenderFrame for the just-completed tick.
    ///
    /// @warning The renderer must finish reading `frame.instances`
    ///          before returning or copy the data it needs; the
    ///          underlying memory may be reused on the next tick.
    virtual void submitFrame(const RenderFrame& frame) = 0;
};

} // namespace threadmaxx
