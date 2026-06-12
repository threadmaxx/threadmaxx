/// @file Delta.cpp

#include "threadmaxx_network/delta.hpp"

#include <algorithm>

namespace threadmaxx::network {

namespace {

bool blobEquals(const std::vector<std::byte>& a,
                const std::vector<std::byte>& b) noexcept {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin());
}

void rebuildIndex(const std::vector<EntityRecord>& src,
                  std::unordered_map<std::uint64_t, std::size_t>& dst) {
    dst.clear();
    dst.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst.emplace(src[i].id.value, i);
    }
}

} // namespace

void DeltaEncoder::setBaseline(TickId tick,
                               std::vector<EntityRecord> baseline) {
    baseline_ = std::move(baseline);
    baselineTick_ = tick;
    rebuildIndex(baseline_, baselineIndex_);
}

std::vector<std::byte>
DeltaEncoder::produceDelta(SessionId session, TickId tick,
                           std::span<const EntityRecord> current,
                           std::uint32_t& nextSequence) {
    BitWriter w;
    PacketHeader hdr{};
    hdr.type = PacketType::Delta;
    hdr.session = session;
    hdr.tick = tick;
    hdr.sequence = ++nextSequence;
    writePacketHeader(w, hdr);
    w.writeBits(baselineTick_.value, 32);

    // Pass 1: changed + spawn entries.
    std::vector<std::size_t> changedIdx;
    changedIdx.reserve(current.size() / 4);
    for (std::size_t i = 0; i < current.size(); ++i) {
        auto bit = baselineIndex_.find(current[i].id.value);
        if (bit == baselineIndex_.end()) {
            // spawn — full record
            changedIdx.push_back(i);
            continue;
        }
        if (!blobEquals(current[i].data, baseline_[bit->second].data)) {
            changedIdx.push_back(i);
        }
    }
    lastChanged_ = static_cast<std::uint32_t>(changedIdx.size());

    // Pass 2: despawn entries — every baseline id not present in current.
    std::vector<NetEntityId> despawned;
    {
        std::unordered_map<std::uint64_t, bool> presentInCurrent;
        presentInCurrent.reserve(current.size());
        for (const auto& r : current) presentInCurrent.emplace(r.id.value, true);
        for (const auto& bRec : baseline_) {
            if (presentInCurrent.find(bRec.id.value) == presentInCurrent.end()) {
                despawned.push_back(bRec.id);
            }
        }
    }
    lastDespawn_ = static_cast<std::uint32_t>(despawned.size());

    w.writeVarUInt(changedIdx.size());
    for (auto i : changedIdx) {
        w.writeBits(current[i].id.value, 64);
        w.writeVarUInt(current[i].data.size());
        w.writeBytes(std::span<const std::byte>{
            current[i].data.data(), current[i].data.size()});
    }
    w.writeVarUInt(despawned.size());
    for (auto id : despawned) {
        w.writeBits(id.value, 64);
    }
    const auto out = w.finish();
    return std::vector<std::byte>{out.begin(), out.end()};
}

void DeltaDecoder::setBaseline(TickId tick,
                               std::vector<EntityRecord> baseline) {
    baseline_ = std::move(baseline);
    baselineTick_ = tick;
    rebuildIndex(baseline_, index_);
}

bool DeltaDecoder::feed(std::span<const std::byte> payload,
                        const EntityReader& reader) {
    BitReader r{payload};
    auto header = readPacketHeader(r);
    if (!header || header->type != PacketType::Delta) return false;
    const TickId base{static_cast<std::uint32_t>(r.readBits(32))};
    if (base != baselineTick_) return false;

    const std::uint64_t changed = r.readVarUInt();
    for (std::uint64_t i = 0; i < changed; ++i) {
        NetEntityId id{r.readBits(64)};
        const std::uint64_t sz = r.readVarUInt();
        std::vector<std::byte> bytes(sz);
        r.readBytes(std::span<std::byte>{bytes.data(), bytes.size()});

        auto it = index_.find(id.value);
        if (it == index_.end()) {
            const std::size_t pos = baseline_.size();
            baseline_.push_back({id, bytes});
            index_.emplace(id.value, pos);
        } else {
            baseline_[it->second].data = bytes;
        }
        BitReader sub{std::span<const std::byte>{bytes.data(), bytes.size()}};
        reader(id, sub);
    }

    const std::uint64_t despawned = r.readVarUInt();
    for (std::uint64_t i = 0; i < despawned; ++i) {
        NetEntityId id{r.readBits(64)};
        auto it = index_.find(id.value);
        if (it == index_.end()) continue;
        // swap-and-pop
        const std::size_t pos = it->second;
        const std::size_t last = baseline_.size() - 1;
        if (pos != last) {
            baseline_[pos] = std::move(baseline_[last]);
            index_[baseline_[pos].id.value] = pos;
        }
        baseline_.pop_back();
        index_.erase(it);
    }

    lastTick_ = header->tick;
    return true;
}

} // namespace threadmaxx::network
