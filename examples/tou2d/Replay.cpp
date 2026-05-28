#include "Replay.hpp"

#include <cstring>

namespace tou2d {

namespace {

constexpr std::uint64_t kEngineHashBasis = 0xcbf29ce484222325ull;

} // namespace

// ─────────────────────────────────────────────────────────── ReplayRecorder

bool ReplayRecorder::open(const std::filesystem::path& path,
                          std::uint8_t numHumans, std::uint8_t numBots,
                          std::uint8_t matchMode, const std::string& levelDir) {
    close();
    file_ = std::fopen(path.string().c_str(), "wb");
    if (!file_) return false;

    ReplayHeader hdr{};
    std::memcpy(hdr.magic, kReplayMagic, 4);
    hdr.version          = kReplayFormatVersion;
    hdr.numHumans        = numHumans;
    hdr.numBots          = numBots;
    hdr.matchMode        = matchMode;
    hdr.engineHashBasis  = kEngineHashBasis;
    hdr.levelDirLen      = static_cast<std::uint16_t>(
        std::min<std::size_t>(levelDir.size(), 0xFFFFu));

    if (std::fwrite(&hdr, sizeof(hdr), 1, file_) != 1) {
        close();
        return false;
    }
    if (hdr.levelDirLen != 0 &&
        std::fwrite(levelDir.data(), 1, hdr.levelDirLen, file_) != hdr.levelDirLen) {
        close();
        return false;
    }

    (void)numHumans;  // captured in header; format always writes 4 rows
    ticksWritten_ = 0;
    return true;
}

void ReplayRecorder::recordTick(std::span<const PlayerInput> keyboardInputs,
                                std::uint64_t                commitHash) {
    if (!file_) return;
    // Always write exactly `kReplayKeyboardSlots` PlayerInputs. Pad
    // with default-zero when the caller passed fewer (defensive); ignore
    // trailing values when they passed more.
    const std::size_t toWrite = std::min<std::size_t>(
        keyboardInputs.size(), kReplayKeyboardSlots);
    if (toWrite > 0) {
        std::fwrite(keyboardInputs.data(), sizeof(PlayerInput), toWrite, file_);
    }
    if (toWrite < kReplayKeyboardSlots) {
        PlayerInput zero{};
        for (std::size_t i = toWrite; i < kReplayKeyboardSlots; ++i) {
            std::fwrite(&zero, sizeof(PlayerInput), 1, file_);
        }
    }
    std::fwrite(&commitHash, sizeof(commitHash), 1, file_);
    ++ticksWritten_;
}

void ReplayRecorder::close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    ticksWritten_ = 0;
}

// ──────────────────────────────────────────────────────────── ReplayPlayer

bool ReplayPlayer::open(const std::filesystem::path& path) {
    close();
    file_ = std::fopen(path.string().c_str(), "rb");
    if (!file_) return false;

    ReplayHeader hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, file_) != 1) {
        close();
        return false;
    }
    if (std::memcmp(hdr.magic, kReplayMagic, 4) != 0)        { close(); return false; }
    if (hdr.version != kReplayFormatVersion)                 { close(); return false; }
    if (hdr.engineHashBasis != kEngineHashBasis)             { close(); return false; }

    if (hdr.levelDirLen > 0) {
        levelDir_.assign(hdr.levelDirLen, '\0');
        if (std::fread(levelDir_.data(), 1, hdr.levelDirLen, file_) != hdr.levelDirLen) {
            close();
            return false;
        }
    }

    numHumans_     = hdr.numHumans;
    numBots_       = hdr.numBots;
    matchMode_     = hdr.matchMode;
    ticksRead_     = 0;
    curCommitHash_ = 0;
    curInputs_.assign(kReplayKeyboardSlots, PlayerInput{});
    return true;
}

void ReplayPlayer::close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    numHumans_     = 0;
    numBots_       = 0;
    matchMode_     = 0;
    ticksRead_     = 0;
    curCommitHash_ = 0;
    curInputs_.clear();
    levelDir_.clear();
}

bool ReplayPlayer::advance() {
    if (!file_) return false;
    const std::size_t got = std::fread(curInputs_.data(),
                                       sizeof(PlayerInput),
                                       kReplayKeyboardSlots,
                                       file_);
    if (got != kReplayKeyboardSlots) return false;
    if (std::fread(&curCommitHash_, sizeof(curCommitHash_), 1, file_) != 1) {
        return false;
    }
    ++ticksRead_;
    return true;
}

} // namespace tou2d
