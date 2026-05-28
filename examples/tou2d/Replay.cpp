#include "Replay.hpp"

#include <cstring>

namespace tou2d {

namespace {

constexpr std::uint64_t kEngineHashBasis = 0xcbf29ce484222325ull;

} // namespace

// ─────────────────────────────────────────────────────────── ReplayRecorder

bool ReplayRecorder::open(const std::filesystem::path& path,
                          std::uint8_t numHumans, std::uint8_t numBots,
                          std::uint8_t matchMode, const std::string& levelDir,
                          const std::optional<ProceduralLevelConfig>& genConfig,
                          std::uint8_t specialKind) {
    close();
    file_ = std::fopen(path.string().c_str(), "wb");
    if (!file_) return false;

    ReplayHeader hdr{};
    std::memcpy(hdr.magic, kReplayMagic, 4);
    hdr.version          = kReplayFormatVersion;
    hdr.numHumans        = numHumans;
    hdr.numBots          = numBots;
    hdr.matchMode        = matchMode;
    hdr.specialKind      = specialKind;  // M5.6
    hdr.engineHashBasis  = kEngineHashBasis;
    // M5.5 — if a generation config is set, levelDir is irrelevant for
    // playback; record an empty string regardless of what the host
    // passed in. Keeps the two playback paths unambiguous.
    const bool gen = genConfig.has_value();
    hdr.useGen     = gen ? 1u : 0u;
    hdr.genLevel   = gen ? genConfig->ggLevel          : 0u;
    hdr.genDensity = gen ? genConfig->stuffDensity     : 0u;
    hdr.genPerim   = gen ? genConfig->perimeterBedrock : 0u;
    hdr.genSeed    = gen ? genConfig->seed             : 0u;
    hdr.levelDirLen = gen ? std::uint16_t{0}
                          : static_cast<std::uint16_t>(
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
    specialKind_   = hdr.specialKind;  // M5.6 (zero in pre-M5.6 v2 recs → Spread)
    if (hdr.useGen != 0) {
        ProceduralLevelConfig g{};
        g.seed             = hdr.genSeed;
        g.ggLevel          = hdr.genLevel;
        g.stuffDensity     = hdr.genDensity;
        g.perimeterBedrock = hdr.genPerim;
        genConfig_         = g;
    } else {
        genConfig_.reset();
    }
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
    specialKind_   = 0;
    ticksRead_     = 0;
    curCommitHash_ = 0;
    curInputs_.clear();
    levelDir_.clear();
    genConfig_.reset();
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
