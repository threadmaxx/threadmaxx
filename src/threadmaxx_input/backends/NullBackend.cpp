#include "threadmaxx_input/backends/NullBackend.hpp"

#include <algorithm>
#include <utility>

namespace threadmaxx::input {

void NullBackend::push(const InputEvent& e) {
    queue_.push_back(e);
}

void NullBackend::pushAll(const InputEvent* begin, std::size_t count) {
    if (count == 0) return;
    queue_.insert(queue_.end(), begin, begin + count);
}

std::size_t NullBackend::poll(InputEvent* out, std::size_t cap) {
    const std::size_t pending = queue_.size() - head_;
    const std::size_t n = std::min(cap, pending);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = queue_[head_ + i];
    }
    head_ += n;

    // Recycle storage once everything is drained so steady state stays
    // allocation-free.
    if (head_ == queue_.size()) {
        queue_.clear();
        head_ = 0;
    }
    return n;
}

void NullBackend::setCursorMode(CursorMode mode) {
    if (cursorMode_ != mode) {
        cursorMode_ = mode;
        ++cursorChangeCount_;
    }
}

void NullBackend::setConnectedGamepads(std::vector<DeviceId> ids) {
    connectedGamepads_ = std::move(ids);
}

std::span<const DeviceId> NullBackend::connectedGamepads() const {
    return connectedGamepads_;
}

void NullBackend::reserve(std::size_t n) {
    if (queue_.capacity() < n) queue_.reserve(n);
}

void NullBackend::clear() noexcept {
    queue_.clear();
    head_ = 0;
}

}  // namespace threadmaxx::input
