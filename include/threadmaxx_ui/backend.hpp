#pragma once

/// @file backend.hpp
/// @brief Renderer adapter interface. `UIContext::endFrame()` hands the
/// finished `DrawList` to whatever `IUIBackend` the host installed. The
/// default backend (`NullBackend`) drops the list — tests use it as a no-op
/// sink; reference backends (Vulkan, UI8) translate to GPU draws.

namespace threadmaxx::ui {

class DrawList;

/// Backend interface. The library never owns one — the host passes its own
/// instance to `UIContext::setBackend()` and is responsible for outliving
/// the context.
class IUIBackend {
public:
    virtual ~IUIBackend() = default;

    /// Called exactly once per frame at `UIContext::endFrame()` after every
    /// widget has emitted. The `DrawList` reference is borrowed; backends
    /// must finish consuming it (or copy out) before returning.
    virtual void submit(const DrawList& list) = 0;
};

} // namespace threadmaxx::ui
