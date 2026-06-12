/// @file Snapshot.cpp
/// @brief SnapshotEncoder + SnapshotDecoder.

#include "threadmaxx_network/replication.hpp"

#include <cstring>

namespace threadmaxx::network {

void SnapshotEncoder::begin(TickId tick) {
    body_ = Body{};
    body_.tick = tick;
    snapshotId_ = nextSnapshotId_++;
    body_.snapshotId = snapshotId_;
    tick_ = tick;
}

void SnapshotEncoder::addEntity(NetEntityId id, const EntityWriter& writer) {
    BitWriter w;
    writer(id, w);
    EntityRecord rec{};
    rec.id = id;
    const auto bytes = w.finish();
    rec.data.assign(bytes.begin(), bytes.end());
    body_.entities.push_back(std::move(rec));
}

std::vector<std::vector<std::byte>>
SnapshotEncoder::finishFragments(SessionId session,
                                 std::uint32_t& nextSequence) {
    // Serialize the body into one large byte stream first.
    BitWriter all;
    all.writeBits(body_.tick.value, 32);
    all.writeBits(body_.snapshotId, 32);
    all.writeVarUInt(body_.entities.size());
    for (const auto& e : body_.entities) {
        all.writeBits(e.id.value, 64);
        all.writeVarUInt(e.data.size());
        all.writeBytes(std::span<const std::byte>{
            e.data.data(), e.data.size()});
    }
    const auto whole = all.finish();
    const std::size_t total = whole.size();

    // Split into fragments.
    const std::size_t fragSize = kSnapshotFragmentBytes;
    const std::uint32_t totalFragments = static_cast<std::uint32_t>(
        (total + fragSize - 1) / fragSize);

    std::vector<std::vector<std::byte>> out;
    out.reserve(totalFragments == 0 ? 1u : totalFragments);
    for (std::uint32_t i = 0;
         i < (totalFragments == 0u ? 1u : totalFragments); ++i) {
        BitWriter pw;
        PacketHeader hdr{};
        hdr.type = PacketType::Snapshot;
        hdr.session = session;
        hdr.tick = body_.tick;
        hdr.sequence = ++nextSequence;
        writePacketHeader(pw, hdr);

        // Per-fragment sub-header: snapshotId, fragmentIdx, totalFragments.
        pw.writeBits(body_.snapshotId, 32);
        pw.writeBits(i, 16);
        pw.writeBits(totalFragments == 0 ? 1u : totalFragments, 16);

        // Payload slice.
        const std::size_t begin = i * fragSize;
        const std::size_t end =
            (begin + fragSize) > total ? total : (begin + fragSize);
        pw.writeBytes(std::span<const std::byte>{
            whole.data() + begin, end - begin});

        const auto pwBytes = pw.finish();
        std::vector<std::byte> bytes(pwBytes.begin(), pwBytes.end());
        out.push_back(std::move(bytes));
    }
    return out;
}

bool SnapshotDecoder::feed(std::span<const std::byte> payload,
                           const EntityReader& reader) {
    BitReader r{payload};
    auto header = readPacketHeader(r);
    if (!header || header->type != PacketType::Snapshot) return false;

    const std::uint32_t snapshotId = static_cast<std::uint32_t>(r.readBits(32));
    const std::uint16_t fragIdx = static_cast<std::uint16_t>(r.readBits(16));
    const std::uint16_t totalFrags = static_cast<std::uint16_t>(r.readBits(16));
    if (r.exhausted() || totalFrags == 0) return false;

    auto& partial = partials_[snapshotId];
    if (partial.totalFragments == 0) {
        partial.totalFragments = totalFrags;
        partial.fragments.assign(totalFrags, {});
        partial.tick = header->tick;
    }
    if (fragIdx >= partial.totalFragments) return false;
    if (partial.fragments[fragIdx].empty()) {
        // Read the rest of the packet as raw payload bytes.
        const std::size_t remaining = payload.size() - r.bytePos();
        partial.fragments[fragIdx].resize(remaining);
        r.readBytes(std::span<std::byte>{
            partial.fragments[fragIdx].data(), remaining});
        ++partial.collected;
    }

    if (partial.collected < partial.totalFragments) return false;

    // Reassemble and decode.
    std::vector<std::byte> whole;
    for (const auto& f : partial.fragments) {
        whole.insert(whole.end(), f.begin(), f.end());
    }
    BitReader br{std::span<const std::byte>{whole.data(), whole.size()}};
    const TickId tick{static_cast<std::uint32_t>(br.readBits(32))};
    (void)br.readBits(32); // snapshotId — already known
    const std::uint64_t entityCount = br.readVarUInt();
    for (std::uint64_t i = 0; i < entityCount; ++i) {
        NetEntityId nid{};
        nid.value = br.readBits(64);
        const std::uint64_t dataSize = br.readVarUInt();
        std::vector<std::byte> sub(dataSize);
        br.readBytes(std::span<std::byte>{sub.data(), sub.size()});
        BitReader sub_r{std::span<const std::byte>{sub.data(), sub.size()}};
        reader(nid, sub_r);
    }
    lastTick_ = tick;
    partials_.erase(snapshotId);
    return true;
}

} // namespace threadmaxx::network
