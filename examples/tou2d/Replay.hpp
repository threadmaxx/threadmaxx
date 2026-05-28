#pragma once

// tou2d M5.4 — replay capture + playback.
//
// On-disk `.tou2drec` format (host-endian, like WorldSnapshot):
//
//   ['T','O','U','R']                          (4  bytes — magic)
//   version            uint32                  (4  bytes — = 1)
//   numHumans          uint8                   (1  byte)
//   numBots            uint8                   (1  byte)
//   matchMode          uint8  (0=DM, 1=LSS)    (1  byte)
//   _pad               uint8  x 5              (5  bytes — keep 16-byte align)
//   engineHashBasis    uint64 (FNV-1a basis)   (8  bytes — sanity check)
//   levelDirLen        uint16                  (2  bytes)
//   _pad2              uint16 + uint32         (6  bytes — pad to 32-byte header)
//   levelDir           char[levelDirLen]       (variable)
//   ── per tick ──
//   PlayerInput        x kReplayKeyboardSlots  (8 bytes each, = 4 → 32 B)
//   commitHash         uint64                  (8 bytes)
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

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace tou2d {

inline constexpr std::uint32_t kReplayFormatVersion  = 1;
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
    std::uint8_t  _pad[5];
    std::uint64_t engineHashBasis;
    std::uint16_t levelDirLen;
    std::uint8_t  _pad2[6];
};
static_assert(sizeof(ReplayHeader) == 32, "ReplayHeader must be 32 bytes");

/// Appends a single tick record per `recordTick()`. Caller responsible
/// for matching the `numHumans` passed at open with the size of the
/// per-tick `humanInputs` span.
class ReplayRecorder {
public:
    ~ReplayRecorder() { close(); }

    bool open(const std::filesystem::path& path,
              std::uint8_t numHumans, std::uint8_t numBots,
              std::uint8_t matchMode, const std::string& levelDir);

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
    std::uint64_t            ticksRead_      = 0;
    std::uint64_t            curCommitHash_  = 0;
    std::vector<PlayerInput> curInputs_;  // size = kReplayKeyboardSlots
};

} // namespace tou2d
