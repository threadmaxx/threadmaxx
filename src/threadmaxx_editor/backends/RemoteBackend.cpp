/// @file backends/RemoteBackend.cpp
/// @brief E14 — encode + decode for the editor's remote wire format.

#include "threadmaxx_editor/backends/remote.hpp"

#include <cstring>

namespace threadmaxx::editor {

namespace {

template <class T>
void appendPod(std::vector<std::byte>& dst, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* src = reinterpret_cast<const std::byte*>(&v);
    dst.insert(dst.end(), src, src + sizeof(T));
}

void appendTag(std::vector<std::byte>& dst, RemoteOpTag tag) {
    dst.push_back(static_cast<std::byte>(tag));
}

template <class T>
bool readPod(std::span<const std::byte>& cur, T& out) {
    if (cur.size() < sizeof(T)) return false;
    std::memcpy(&out, cur.data(), sizeof(T));
    cur = cur.subspan(sizeof(T));
    return true;
}

} // namespace

void RemoteBackend::beginFrame() {
    appendTag(buffer_, RemoteOpTag::BeginFrame);
}

void RemoteBackend::endFrame() {
    appendTag(buffer_, RemoteOpTag::EndFrame);
}

void RemoteBackend::drawText(std::string_view text, float x, float y) {
    appendTag(buffer_, RemoteOpTag::DrawText);
    appendPod(buffer_, x);
    appendPod(buffer_, y);
    const auto len = static_cast<std::uint32_t>(text.size());
    appendPod(buffer_, len);
    const auto* src = reinterpret_cast<const std::byte*>(text.data());
    buffer_.insert(buffer_.end(), src, src + text.size());
}

void RemoteBackend::drawRect(float x, float y, float w, float h) {
    appendTag(buffer_, RemoteOpTag::DrawRect);
    appendPod(buffer_, x);
    appendPod(buffer_, y);
    appendPod(buffer_, w);
    appendPod(buffer_, h);
}

std::size_t decodeRemoteStream(std::span<const std::byte> bytes,
                               IEditorBackend& sink) {
    auto cur = bytes;
    while (!cur.empty()) {
        const auto rawTag = static_cast<std::uint8_t>(cur[0]);
        cur = cur.subspan(1);
        switch (static_cast<RemoteOpTag>(rawTag)) {
            case RemoteOpTag::BeginFrame:
                sink.beginFrame();
                break;
            case RemoteOpTag::EndFrame:
                sink.endFrame();
                break;
            case RemoteOpTag::DrawText: {
                float x{};
                float y{};
                std::uint32_t len{};
                if (!readPod(cur, x) || !readPod(cur, y)
                 || !readPod(cur, len)) {
                    return 0;
                }
                if (cur.size() < len) return 0;
                const auto* p = reinterpret_cast<const char*>(cur.data());
                sink.drawText(std::string_view(p, len), x, y);
                cur = cur.subspan(len);
                break;
            }
            case RemoteOpTag::DrawRect: {
                float x{};
                float y{};
                float w{};
                float h{};
                if (!readPod(cur, x) || !readPod(cur, y)
                 || !readPod(cur, w) || !readPod(cur, h)) {
                    return 0;
                }
                sink.drawRect(x, y, w, h);
                break;
            }
            default:
                return 0;
        }
    }
    return bytes.size();
}

} // namespace threadmaxx::editor
