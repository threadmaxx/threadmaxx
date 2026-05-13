#pragma once

#include "Engine.hpp"

#include <cstddef>
#include <mutex>
#include <span>
#include <typeindex>
#include <utility>
#include <vector>

namespace threadmaxx {

/// Typed double-buffered event queue for cross-system messaging.
///
/// Producers (any thread, including worker jobs) call @ref emit; consumers
/// (typically other systems) call @ref drainTick once per tick to read
/// the events that were emitted during the previous tick. The double
/// buffer flips on the tick boundary — the engine calls `drain()`
/// internally between `postStep` and the next `preStep`.
///
/// @par Lifetime
///      Channels are engine-owned and shared. Get one via
///      `Engine::events<MyEvent>()`. The same channel is returned across
///      calls, including from worker jobs.
/// @par Thread-safety
///      `emit` is safe from any thread. `drainTick` is intended for
///      single-threaded consumption from a system body during `update`
///      and returns a span into the engine-owned read buffer — copy if
///      you need to retain it past the tick.
template <typename Ev>
class EventChannel {
public:
    EventChannel() = default;

    EventChannel(const EventChannel&) = delete;
    EventChannel& operator=(const EventChannel&) = delete;

    /// Append an event to the back buffer; visible to readers next tick.
    /// @thread_safety Safe from any thread.
    void emit(const Ev& ev) {
        std::lock_guard<std::mutex> lk(backMtx_);
        back_.push_back(ev);
    }
    void emit(Ev&& ev) {
        std::lock_guard<std::mutex> lk(backMtx_);
        back_.push_back(std::move(ev));
    }

    /// Events emitted on the previous tick. Span is stable until the
    /// next engine drain (i.e. for the duration of this tick's wave
    /// execution).
    std::span<const Ev> drainTick() const noexcept {
        return std::span<const Ev>(front_.data(), front_.size());
    }

    /// @internal Engine-called: swap front/back, clear new back.
    /// Single-threaded; runs on the sim thread at tick boundary.
    void drain() {
        front_.clear();
        std::lock_guard<std::mutex> lk(backMtx_);
        front_.swap(back_);
    }

    /// Events currently sitting in the back buffer (about to be drained).
    /// Mostly useful for tests.
    std::size_t pendingCount() const noexcept {
        std::lock_guard<std::mutex> lk(backMtx_);
        return back_.size();
    }

private:
    std::vector<Ev>    front_;       ///< readers consume from here
    std::vector<Ev>    back_;        ///< writers append here, guarded by backMtx_
    mutable std::mutex backMtx_;
};

template <typename Ev>
EventChannel<Ev>& Engine::events() {
    // Stateless captureless lambdas decay to function pointers; the
    // engine stores those alongside the type-erased channel pointer
    // and uses them for ~Engine() teardown and per-tick drain.
    constexpr auto factory = []() -> void* { return new EventChannel<Ev>(); };
    constexpr auto deleter = [](void* p) {
        delete static_cast<EventChannel<Ev>*>(p);
    };
    constexpr auto drainFn = [](void* p) {
        static_cast<EventChannel<Ev>*>(p)->drain();
    };
    void* raw = getEventChannelRaw(std::type_index(typeid(Ev)),
                                   +factory, +deleter, +drainFn);
    return *static_cast<EventChannel<Ev>*>(raw);
}

} // namespace threadmaxx
