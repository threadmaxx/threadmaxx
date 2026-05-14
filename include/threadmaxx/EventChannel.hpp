#pragma once

#include "Engine.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
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
    /// Identifier for a persistent subscription. Pass to
    /// @ref unsubscribe to detach. Zero is reserved as the "invalid"
    /// id; @ref subscribe never returns zero.
    using SubscriptionId = std::uint64_t;
    /// Callback signature for persistent subscribers.
    using Callback       = std::function<void(const Ev&)>;

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

    /// Register a persistent callback that the engine invokes once per
    /// event during the tick-boundary drain — i.e. for each event
    /// emitted during the just-finished tick. Cheaper than maintaining
    /// a manual `drainTick()`-in-postStep loop when several systems
    /// want to observe the same channel.
    ///
    /// @return A non-zero id usable with @ref unsubscribe. Subscription
    ///         order is preserved; callbacks are invoked in
    ///         subscription order.
    /// @thread_safety Safe from any thread. The callback itself fires
    ///                on the sim thread during drain and runs to
    ///                completion before the front/back swap, so a
    ///                callback that re-emits sees its own events on
    ///                the next tick.
    SubscriptionId subscribe(Callback cb) {
        std::lock_guard<std::mutex> lk(subscribersMtx_);
        const SubscriptionId id = ++nextId_;
        subscribers_.push_back({id, std::move(cb)});
        return id;
    }

    /// Remove a previously-registered subscriber. No-op if the id is
    /// not currently registered. Safe to call from inside a callback.
    /// @thread_safety Safe from any thread.
    bool unsubscribe(SubscriptionId id) {
        std::lock_guard<std::mutex> lk(subscribersMtx_);
        for (auto it = subscribers_.begin(); it != subscribers_.end(); ++it) {
            if (it->id == id) {
                subscribers_.erase(it);
                return true;
            }
        }
        return false;
    }

    /// Number of currently-registered subscribers. Mostly useful for
    /// tests and HUD readouts.
    std::size_t subscriberCount() const noexcept {
        std::lock_guard<std::mutex> lk(subscribersMtx_);
        return subscribers_.size();
    }

    /// @internal Engine-called: invoke each subscriber once per event
    /// in the back buffer (the events about to become readable), then
    /// swap front/back and clear the new back. Single-threaded; runs
    /// on the sim thread at tick boundary.
    void drain() {
        // Snapshot subscribers so a callback that mutates the list
        // (subscribe/unsubscribe) cannot invalidate our iterator.
        std::vector<Callback> snapshot;
        {
            std::lock_guard<std::mutex> lk(subscribersMtx_);
            snapshot.reserve(subscribers_.size());
            for (const auto& s : subscribers_) snapshot.push_back(s.callback);
        }
        if (!snapshot.empty()) {
            std::lock_guard<std::mutex> lk(backMtx_);
            for (const auto& ev : back_) {
                for (auto& cb : snapshot) cb(ev);
            }
        }
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
    struct Subscriber {
        SubscriptionId id;
        Callback       callback;
    };

    std::vector<Ev>    front_;       ///< readers consume from here
    std::vector<Ev>    back_;        ///< writers append here, guarded by backMtx_
    mutable std::mutex backMtx_;

    // §3.1 persistent subscriptions. Drain invokes each callback once
    // per back-buffered event before the front/back swap, so the
    // callback sees this tick's emits (matching the next tick's
    // drainTick() consumer view).
    std::vector<Subscriber> subscribers_;
    mutable std::mutex      subscribersMtx_;
    SubscriptionId          nextId_ = 0;
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
