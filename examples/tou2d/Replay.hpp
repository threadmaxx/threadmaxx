#pragma once

// tou2d M5.4 — replay capture + playback.
// tou2d M5.5 — bumped to v2 to carry a procedural-generation config.
//
// On-disk `.tou2drec` format v2 (host-endian, like WorldSnapshot):
//
//   ['T','O','U','R']                          (4  bytes — magic)
//   version            uint32                  (4  bytes — = 2)
//   numHumans          uint8                   (1  byte)
//   numBots            uint8                   (1  byte)
//   matchMode          uint8  (0=DM, 1=LSS)    (1  byte)
//   useGen             uint8  (0/1)            (1  byte) ── M5.5
//   genLevel           uint8  (0..4)           (1  byte) ── M5.5
//   genDensity         uint8  (0..100)         (1  byte) ── M5.5
//   genPerim           uint8  (0/1)            (1  byte) ── M5.5
//   _pad               uint8                   (1  byte)
//   engineHashBasis    uint64 (FNV-1a basis)   (8  bytes — sanity check)
//   levelDirLen        uint16                  (2  bytes)
//   _pad2              uint16                  (2  bytes)
//   genSeed            uint32                  (4  bytes) ── M5.5
//   ──────────────────                         (32 bytes total)
//   levelDir           char[levelDirLen]       (variable, can be 0)
//   ── per tick ──
//   PlayerInput        x kReplayKeyboardSlots  (8 bytes each, = 4 → 32 B)
//   commitHash         uint64                  (8 bytes)
//
// useGen=1 means the recording used the M5.5 procedural generator —
// `levelDir` is empty in that case and playback reconstructs the level
// from (genSeed, genLevel, genDensity, genPerim). useGen=0 retains the
// pre-M5.5 levelDir-or-arena posture.
//
// v1 recordings produced before M5.5 cannot be read by the v2 player
// (version mismatch refuses the open). That's the intended break: the
// header now carries extra fields whose absence in v1 made playback
// ambiguous (was it gen or load?).
//
// The tick index is implicit by record position. Bots are NOT captured:
// their state is re-derivable from the registered match config + their
// per-slot deterministic mt19937 seed (set in BotControlSystem ctor),
// so the recorder need only persist human keypress streams. We capture
// all 4 keyboard rows (not just the first `numHumans`) because
// `InputSystem` calls `readKeys(slot)` for every LocalPlayer entity:
// slots [numHumans, 4) belong to bots, but if anyone is wiggling those
// WASD/IJKL/numpad keys, the read is nonzero and lands in the
// commitHash stream BEFORE `BotControlSystem` overwrites the slot.
// Persisting all 4 rows kills that divergence with no behaviour change
// vs the pre-M5.4 InputSystem. Slots ≥ 4 read zero in both modes — no
// capture needed there.

#include "DemoTypes.hpp"
#include "ProceduralLevel.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tou2d {

inline constexpr std::uint32_t kReplayFormatVersion  = 2;  // M5.5 — bumped from 1
inline constexpr char          kReplayMagic[4]       = {'T','O','U','R'};
inline constexpr std::uint8_t  kReplayKeyboardSlots  = 4;  // P1..P4 rows

/// 32-byte on-disk header. Followed by `levelDirLen` raw chars and then
/// `tickCount` tick records.
struct ReplayHeader {
    char          magic[4];
    std::uint32_t version;
    std::uint8_t  numHumans;
    std::uint8_t  numBots;
    std::uint8_t  matchMode;
    std::uint8_t  useGen;       ///< M5.5 — 0=load levelDir, 1=use genConfig
    std::uint8_t  genLevel;     ///< M5.5 — ProceduralLevelConfig::ggLevel
    std::uint8_t  genDensity;   ///< M5.5 — ProceduralLevelConfig::stuffDensity
    std::uint8_t  genPerim;     ///< M5.5 — ProceduralLevelConfig::perimeterBedrock
    std::uint8_t  _pad;
    std::uint64_t engineHashBasis;
    std::uint16_t levelDirLen;
    std::uint16_t _pad2;
    std::uint32_t genSeed;      ///< M5.5 — ProceduralLevelConfig::seed
};
static_assert(sizeof(ReplayHeader) == 32, "ReplayHeader must be 32 bytes");

/// Appends a single tick record per `recordTick()`. Caller responsible
/// for matching the `numHumans` passed at open with the size of the
/// per-tick `humanInputs` span.
class ReplayRecorder {
public:
    ~ReplayRecorder() { close(); }

    /// @param genConfig if set, the recording used the M5.5 procedural
    ///        generator — `levelDir` is ignored and the four gen fields
    ///        are written into the header so playback can rebuild the
    ///        same level deterministically.
    bool open(const std::filesystem::path& path,
              std::uint8_t numHumans, std::uint8_t numBots,
              std::uint8_t matchMode, const std::string& levelDir,
              const std::optional<ProceduralLevelConfig>& genConfig = std::nullopt);

    /// @param keyboardInputs span of exactly `kReplayKeyboardSlots` (=4)
    ///        `PlayerInput` values — one per keyboard row regardless of
    ///        the actual human count. Slots without a human still get
    ///        whatever the keyboard reader observed, so the recorded
    ///        stream matches the live `commitHash` byte-for-byte.
    void recordTick(std::span<const PlayerInput> keyboardInputs,
                    std::uint64_t                commitHash);

    void close();

    bool          valid()         const noexcept { return file_ != nullptr; }
    std::uint64_t ticksWritten()  const noexcept { return ticksWritten_;    }

private:
    std::FILE*    file_         = nullptr;
    std::uint64_t ticksWritten_ = 0;
};

/// Streams tick records from disk. `advance()` reads the next record;
/// `inputs(slot)` and `commitHash()` reflect the last successful read.
class ReplayPlayer {
public:
    ~ReplayPlayer() { close(); }

    bool open(const std::filesystem::path& path);
    void close();

    bool                valid()     const noexcept { return file_ != nullptr; }
    std::uint8_t        numHumans() const noexcept { return numHumans_;       }
    std::uint8_t        numBots()   const noexcept { return numBots_;         }
    std::uint8_t        matchMode() const noexcept { return matchMode_;       }
    const std::string&  levelDir()  const noexcept { return levelDir_;        }

    /// M5.5 — set iff the recording used the procedural generator. Host
    /// should call `TouGame::setGenerationConfig(*genConfig())` BEFORE
    /// `engine.initialize(game)` so onSetup rebuilds the same level.
    const std::optional<ProceduralLevelConfig>& genConfig() const noexcept {
        return genConfig_;
    }

    /// Read next tick record. Returns true on success, false at clean
    /// EOF or on read error. After a successful read, `commitHash()`
    /// returns the recorded post-step hash for that tick.
    bool advance();

    /// Mirrors keyboard-mode behaviour: slot < 4 returns the recorded
    /// keyboard-row value, slot ≥ 4 returns a default-zero input
    /// (same as `readKeys()` returns when called with an out-of-range
    /// slot).
    PlayerInput   inputs(std::uint8_t slot) const noexcept {
        if (slot >= kReplayKeyboardSlots) return PlayerInput{};
        return curInputs_[slot];
    }
    std::uint64_t commitHash()  const noexcept { return curCommitHash_; }
    std::uint64_t ticksRead()   const noexcept { return ticksRead_;     }

private:
    std::FILE*               file_           = nullptr;
    std::uint8_t             numHumans_      = 0;
    std::uint8_t             numBots_        = 0;
    std::uint8_t             matchMode_      = 0;
    std::string              levelDir_;
    std::optional<ProceduralLevelConfig> genConfig_;
    std::uint64_t            ticksRead_      = 0;
    std::uint64_t            curCommitHash_  = 0;
    std::vector<PlayerInput> curInputs_;  // size = kReplayKeyboardSlots
};

} // namespace tou2d
