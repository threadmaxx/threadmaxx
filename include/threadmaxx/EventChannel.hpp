#pragma once

#include "Engine.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <typeindex>
#include <utility>
#include <vector>

namespace threadmaxx {

/// RAII handle returned by @ref EventChannel::subscribeScoped (§3.2
/// batch 7). On destruction (or @ref reset) it detaches the underlying
/// subscription. Move-only. Safe to outlive the channel: the handle
/// holds a `weak_ptr` to the channel's subscriber list, so if the
/// channel is destroyed first the dtor no-ops.
///
/// Type-erased so it can be stored in containers without templating
/// on the event type.
class Subscription {
public:
    using SubscriptionId = std::uint64_t;

    Subscription() = default;

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept
        : list_(std::move(other.list_)),
          unsubscribe_(other.unsubscribe_),
          id_(other.id_) {
        other.unsubscribe_ = nullptr;
        other.id_          = 0;
    }

    Subscription& operator=(Subscription&& other) noexcept {
        if (this == &other) return *this;
        reset();
        list_         = std::move(other.list_);
        unsubscribe_  = other.unsubscribe_;
        id_           = other.id_;
        other.unsubscribe_ = nullptr;
        other.id_          = 0;
        return *this;
    }

    ~Subscription() { reset(); }

    /// True iff this handle still represents an attached subscription.
    /// Returns false after `reset()` or after the source channel has
    /// been destroyed.
    bool valid() const noexcept {
        return id_ != 0 && unsubscribe_ && !list_.expired();
    }
    explicit operator bool() const noexcept { return valid(); }

    /// Detach now (no-op if already detached / channel destroyed).
    void reset() noexcept {
        if (id_ != 0 && unsubscribe_) {
            if (auto locked = list_.lock()) {
                unsubscribe_(locked.get(), id_);
            }
        }
        list_.reset();
        unsubscribe_ = nullptr;
        id_          = 0;
    }

    /// The underlying numeric id, for compatibility with the legacy
    /// `subscribe` / `unsubscribe` interface. Zero if invalid.
    SubscriptionId id() const noexcept { return id_; }

private:
    template <typename Ev> friend class EventChannel;

    using UnsubscribeFn = void (*)(void*, SubscriptionId) noexcept;

    Subscription(std::weak_ptr<void> list,
                 UnsubscribeFn fn,
                 SubscriptionId id) noexcept
        : list_(std::move(list)), unsubscribe_(fn), id_(id) {}

    std::weak_ptr<void> list_;
    UnsubscribeFn       unsubscribe_ = nullptr;
    SubscriptionId      id_          = 0;
};

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
///      `emit` is safe from any thread and uses a lock-free Treiber-stack
///      MPSC queue (§3.6 batch 13c) — producers contend only on the
///      head pointer's CAS, never on a mutex. `drainTick` is intended
///      for single-threaded consumption from a system body during
///      `update` and returns a span into the engine-owned read buffer
///      — copy if you need to retain it past the tick.
/// @par Ordering
///      Within a single producer thread, emit order is preserved on
///      drain. Across producer threads the interleaving is undefined
///      (it was already racing on the previous mutex-protected
///      implementation; locks serialize but threads have no way to
///      observe that order anyway). Subscribers fire in the same order
///      `drainTick` would yield.
template <typename Ev>
class EventChannel {
public:
    /// Identifier for a persistent subscription. Pass to
    /// @ref unsubscribe to detach. Zero is reserved as the "invalid"
    /// id; @ref subscribe never returns zero.
    using SubscriptionId = Subscription::SubscriptionId;
    /// Callback signature for persistent subscribers.
    using Callback       = std::function<void(const Ev&)>;

    EventChannel() : subscribers_(std::make_shared<SubscriberList>()) {}

    ~EventChannel() {
        // Free any back-buffer nodes that did not survive to a final
        // drain (e.g. destruction during shutdown without a tick).
        Node* h = backHead_.load(std::memory_order_acquire);
        while (h) {
            Node* next = h->next;
            delete h;
            h = next;
        }
    }

    EventChannel(const EventChannel&) = delete;
    EventChannel& operator=(const EventChannel&) = delete;

    /// Append an event to the back buffer; visible to readers next tick.
    /// @thread_safety Safe from any thread. Lock-free CAS prepend.
    void emit(const Ev& ev) {
        auto* n = new Node{ev, nullptr};
        prependBack(n);
    }
    void emit(Ev&& ev) {
        auto* n = new Node{std::move(ev), nullptr};
        prependBack(n);
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
    ///                completion before the front/back swap. A
    ///                callback that re-emits attaches its events to
    ///                the new tick (the lock-free emit path captures
    ///                them on `backHead_`, which drain has already
    ///                cleared).
    SubscriptionId subscribe(Callback cb) {
        std::lock_guard<std::mutex> lk(subscribers_->mtx);
        const SubscriptionId id = ++subscribers_->nextId;
        subscribers_->subs.push_back({id, std::move(cb)});
        return id;
    }

    /// RAII flavor of @ref subscribe (§3.2 batch 7). Returns a
    /// @ref Subscription that auto-detaches on destruction. The
    /// `Subscription` is type-erased and safe to outlive the channel.
    /// @thread_safety Same as @ref subscribe.
    Subscription subscribeScoped(Callback cb) {
        const SubscriptionId id = subscribe(std::move(cb));
        std::shared_ptr<void> erased = subscribers_;
        return Subscription(std::weak_ptr<void>(erased),
                            &unsubscribeImpl_, id);
    }

    /// Remove a previously-registered subscriber. No-op if the id is
    /// not currently registered. Safe to call from inside a callback.
    /// @thread_safety Safe from any thread.
    bool unsubscribe(SubscriptionId id) {
        std::lock_guard<std::mutex> lk(subscribers_->mtx);
        auto& subs = subscribers_->subs;
        for (auto it = subs.begin(); it != subs.end(); ++it) {
            if (it->id == id) {
                subs.erase(it);
                return true;
            }
        }
        return false;
    }

    /// Number of currently-registered subscribers. Mostly useful for
    /// tests and HUD readouts.
    std::size_t subscriberCount() const noexcept {
        std::lock_guard<std::mutex> lk(subscribers_->mtx);
        return subscribers_->subs.size();
    }

    /// @internal Engine-called: invoke each subscriber once per event
    /// in the back buffer (the events about to become readable), then
    /// swap front/back and clear the new back. Single-threaded; runs
    /// on the sim thread at tick boundary.
    void drain() {
        // Snapshot subscribers so a callback that mutates the list
        // (subscribe/unsubscribe) cannot invalidate our iteration.
        std::vector<Callback> snapshot;
        {
            std::lock_guard<std::mutex> lk(subscribers_->mtx);
            snapshot.reserve(subscribers_->subs.size());
            for (const auto& s : subscribers_->subs) snapshot.push_back(s.callback);
        }

        // Atomically detach the entire back stack.
        Node* head = backHead_.exchange(nullptr, std::memory_order_acquire);
        backCount_.store(0, std::memory_order_relaxed);

        // Walk the LIFO chain, moving events into `front_` and freeing
        // nodes; then reverse to restore per-thread FIFO emit order.
        front_.clear();
        while (head) {
            Node* next = head->next;
            front_.push_back(std::move(head->value));
            delete head;
            head = next;
        }
        std::reverse(front_.begin(), front_.end());

        // Fire subscribers in emit order. Subscribers that re-emit
        // publish onto the new (currently-empty) back stack — this is
        // race-free because we already exchanged `backHead_` to null.
        if (!snapshot.empty()) {
            for (const auto& ev : front_) {
                for (auto& cb : snapshot) cb(ev);
            }
        }
    }

    /// Events currently sitting in the back buffer (about to be drained).
    /// Approximate under concurrent emit; useful for tests and HUDs.
    std::size_t pendingCount() const noexcept {
        return backCount_.load(std::memory_order_relaxed);
    }

private:
    struct Subscriber {
        SubscriptionId id;
        Callback       callback;
    };
    // Held by `shared_ptr` so a `Subscription` can hold a `weak_ptr` to
    // it and safely no-op when this channel is destroyed before the
    // Subscription (e.g. game-side state outliving Engine::shutdown).
    struct SubscriberList {
        mutable std::mutex      mtx;
        std::vector<Subscriber> subs;
        SubscriptionId          nextId = 0;
    };

    // Lock-free MPSC node. New nodes prepend onto `backHead_`; drain
    // exchanges the head pointer to nullptr in one atomic op.
    struct Node {
        Ev    value;
        Node* next;
    };

    void prependBack(Node* n) noexcept {
        Node* old = backHead_.load(std::memory_order_relaxed);
        do {
            n->next = old;
        } while (!backHead_.compare_exchange_weak(
            old, n,
            std::memory_order_release,
            std::memory_order_relaxed));
        backCount_.fetch_add(1, std::memory_order_relaxed);
    }

    // Per-Ev static used by Subscription to detach without templating.
    // Generated once per Ev type by the linker.
    static void unsubscribeImpl_(void* listPtr, SubscriptionId id) noexcept {
        auto* list = static_cast<SubscriberList*>(listPtr);
        std::lock_guard<std::mutex> lk(list->mtx);
        for (auto it = list->subs.begin(); it != list->subs.end(); ++it) {
            if (it->id == id) {
                list->subs.erase(it);
                return;
            }
        }
    }

    std::vector<Ev>          front_;                ///< sim-thread reader; drained from `backHead_` each tick
    std::atomic<Node*>       backHead_{nullptr};    ///< MPSC stack head
    std::atomic<std::size_t> backCount_{0};         ///< approximate pending count for tests/HUDs

    std::shared_ptr<SubscriberList> subscribers_;
};

template <typename Ev>
EventChannel<Ev>& Engine::events() {
    // §3.10.2 batch 22 — F8 considered, NOT shipped. A `thread_local`
    // pointer cache here looked attractive (skip the engine's
    // `eventChannelsMtx_` on every call), but the implementation
    // broke under test workloads that create and destroy engines
    // back-to-back: the cached pointer dangles when a fresh engine
    // happens to land at the same address as a destroyed one. The
    // safe variants (per-instance version counter, per-thread map
    // keyed by `this`) are more bookkeeping than the ~30 ns mutex
    // acquire is worth. Documented as "warm channels at setup" on
    // the public API instead — see `Engine::events` comment.
    //
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
