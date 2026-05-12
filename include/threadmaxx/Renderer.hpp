#pragma once

#include "RenderFrame.hpp"

namespace threadmaxx {

// Renderer-side interface. The engine owns scheduling and snapshotting; the
// renderer is told "here is the frame to draw". A null renderer is allowed
// (headless mode) and the engine simply skips submitFrame() calls.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Called once when the engine starts. Return false on a fatal error to
    // abort startup.
    virtual bool initialize() { return true; }

    // Called once at shutdown after the final frame has been submitted.
    virtual void shutdown() {}

    // Called from the simulation thread after each commit, with the
    // engine-owned RenderFrame for the just-completed tick. The renderer
    // must finish reading `frame.instances` before returning or copy the
    // data it needs; the underlying memory may be reused on the next tick.
    virtual void submitFrame(const RenderFrame& frame) = 0;
};

} // namespace threadmaxx
