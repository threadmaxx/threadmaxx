#pragma once

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// Skeleton + clip serialization for asset save/load.
///
/// Wire format mirrors the engine's `WorldSnapshot` convention:
/// `[magic 'AMTX' u32][version u32]` header followed by per-POD
/// fields. All numeric fields are written host-endian (same caveat
/// as `WorldSnapshot`): cross-process / cross-arch transfer is
/// out of scope for v1.0 — the format is for local on-disk cache
/// and bake-step output. v1.x can ship a byte-swapping reader when
/// it's actually needed.
///
/// `kAnimationAssetVersion` bumps on any field addition. The reader
/// rejects mismatched magic OR version and returns `std::nullopt`.
namespace threadmaxx::animation {

inline constexpr std::uint32_t kAnimationAssetMagic
    = (static_cast<std::uint32_t>('A')      )
    | (static_cast<std::uint32_t>('M') <<  8)
    | (static_cast<std::uint32_t>('T') << 16)
    | (static_cast<std::uint32_t>('X') << 24);

inline constexpr std::uint32_t kAnimationAssetVersion = 1;

/// Container POD round-tripped by the writer/reader. Game-side code
/// that wants to bake a single skeleton + clip set hands one of these
/// straight to `writeAnimationAssetBundle`.
struct AnimationAssetBundle {
    std::vector<SkeletonDesc> skeletons;
    std::vector<ClipDesc> clips;
};

namespace detail {

inline void writeBytes(std::vector<std::uint8_t>& out,
                       const void* src,
                       std::size_t n) {
    const std::size_t pos = out.size();
    out.resize(pos + n);
    std::memcpy(out.data() + pos, src, n);
}

template <class T>
inline void writePod(std::vector<std::uint8_t>& out, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    writeBytes(out, &v, sizeof(T));
}

inline void writeString(std::vector<std::uint8_t>& out,
                        const std::string& s) {
    const std::uint64_t n = s.size();
    writePod(out, n);
    if (n) writeBytes(out, s.data(), s.size());
}

template <class T>
inline void writePodVec(std::vector<std::uint8_t>& out,
                        const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const std::uint64_t n = v.size();
    writePod(out, n);
    if (n) writeBytes(out, v.data(), v.size() * sizeof(T));
}

struct Reader {
    const std::uint8_t* p;
    const std::uint8_t* end;

    bool readBytes(void* dst, std::size_t n) noexcept {
        if (static_cast<std::size_t>(end - p) < n) return false;
        std::memcpy(dst, p, n);
        p += n;
        return true;
    }

    template <class T>
    bool readPod(T& out) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        return readBytes(&out, sizeof(T));
    }

    bool readString(std::string& out) {
        std::uint64_t n = 0;
        if (!readPod(n)) return false;
        if (static_cast<std::size_t>(end - p) < n) return false;
        out.assign(reinterpret_cast<const char*>(p),
                   static_cast<std::size_t>(n));
        p += n;
        return true;
    }

    template <class T>
    bool readPodVec(std::vector<T>& out) {
        static_assert(std::is_trivially_copyable_v<T>);
        std::uint64_t n = 0;
        if (!readPod(n)) return false;
        if (static_cast<std::size_t>(end - p) < n * sizeof(T)) return false;
        out.resize(static_cast<std::size_t>(n));
        if (n) std::memcpy(out.data(), p, n * sizeof(T));
        p += n * sizeof(T);
        return true;
    }
};

inline void writeJoint(std::vector<std::uint8_t>& out, const Joint& j) {
    writeString(out, j.name);
    writePod(out, static_cast<std::int32_t>(j.parent));
    writePod(out, j.bindLocal);
}

inline bool readJoint(Reader& r, Joint& j) {
    if (!r.readString(j.name)) return false;
    std::int32_t parent = 0;
    if (!r.readPod(parent)) return false;
    j.parent = parent;
    if (!r.readPod(j.bindLocal)) return false;
    return true;
}

inline void writeSkeleton(std::vector<std::uint8_t>& out,
                          const SkeletonDesc& s) {
    writeString(out, s.name);
    const std::uint64_t jn = s.joints.size();
    writePod(out, jn);
    for (const auto& j : s.joints) writeJoint(out, j);
    writePodVec(out, s.bindGlobal);
}

inline bool readSkeleton(Reader& r, SkeletonDesc& s) {
    if (!r.readString(s.name)) return false;
    std::uint64_t jn = 0;
    if (!r.readPod(jn)) return false;
    s.joints.resize(static_cast<std::size_t>(jn));
    for (auto& j : s.joints) {
        if (!readJoint(r, j)) return false;
    }
    if (!r.readPodVec(s.bindGlobal)) return false;
    return true;
}

inline void writeEvent(std::vector<std::uint8_t>& out,
                       const EventTrackEvent& e) {
    writePod(out, e.time);
    writeString(out, e.name);
}

inline bool readEvent(Reader& r, EventTrackEvent& e) {
    if (!r.readPod(e.time)) return false;
    if (!r.readString(e.name)) return false;
    return true;
}

inline void writeClip(std::vector<std::uint8_t>& out, const ClipDesc& c) {
    writeString(out, c.name);
    writePod(out, c.duration);
    writePod(out, static_cast<std::uint8_t>(c.looping ? 1u : 0u));
    const std::uint64_t en = c.events.size();
    writePod(out, en);
    for (const auto& e : c.events) writeEvent(out, e);
    writePod(out, c.jointCount);
    writePodVec(out, c.keyframeTimes);
    writePodVec(out, c.keyframes);
}

inline bool readClip(Reader& r, ClipDesc& c) {
    if (!r.readString(c.name)) return false;
    if (!r.readPod(c.duration)) return false;
    std::uint8_t looping = 0;
    if (!r.readPod(looping)) return false;
    c.looping = (looping != 0);
    std::uint64_t en = 0;
    if (!r.readPod(en)) return false;
    c.events.resize(static_cast<std::size_t>(en));
    for (auto& e : c.events) {
        if (!readEvent(r, e)) return false;
    }
    if (!r.readPod(c.jointCount)) return false;
    if (!r.readPodVec(c.keyframeTimes)) return false;
    if (!r.readPodVec(c.keyframes)) return false;
    return true;
}

} // namespace detail

/// Write the bundle to a freshly-allocated byte buffer. Format:
/// `[magic u32][version u32][skeletonCount u64](skeleton ...)*
///  [clipCount u64](clip ...)*`.
inline std::vector<std::uint8_t> writeAnimationAssetBundle(
    const AnimationAssetBundle& bundle) {
    std::vector<std::uint8_t> bytes;
    detail::writePod(bytes, kAnimationAssetMagic);
    detail::writePod(bytes, kAnimationAssetVersion);
    const std::uint64_t sn = bundle.skeletons.size();
    detail::writePod(bytes, sn);
    for (const auto& s : bundle.skeletons) detail::writeSkeleton(bytes, s);
    const std::uint64_t cn = bundle.clips.size();
    detail::writePod(bytes, cn);
    for (const auto& c : bundle.clips) detail::writeClip(bytes, c);
    return bytes;
}

/// Read a bundle from a byte span. Returns `std::nullopt` on magic
/// mismatch, version mismatch, or truncated input.
inline std::optional<AnimationAssetBundle> readAnimationAssetBundle(
    std::span<const std::uint8_t> bytes) {
    detail::Reader r{bytes.data(), bytes.data() + bytes.size()};
    std::uint32_t magic = 0, version = 0;
    if (!r.readPod(magic) || magic != kAnimationAssetMagic)
        return std::nullopt;
    if (!r.readPod(version) || version != kAnimationAssetVersion)
        return std::nullopt;

    AnimationAssetBundle bundle;
    std::uint64_t sn = 0;
    if (!r.readPod(sn)) return std::nullopt;
    bundle.skeletons.resize(static_cast<std::size_t>(sn));
    for (auto& s : bundle.skeletons) {
        if (!detail::readSkeleton(r, s)) return std::nullopt;
    }
    std::uint64_t cn = 0;
    if (!r.readPod(cn)) return std::nullopt;
    bundle.clips.resize(static_cast<std::size_t>(cn));
    for (auto& c : bundle.clips) {
        if (!detail::readClip(r, c)) return std::nullopt;
    }
    return bundle;
}

} // namespace threadmaxx::animation
