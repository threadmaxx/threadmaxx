// M4.8 — miniaudio-backed AudioSystem implementation.
//
// Owns:
//   * `ma_engine`        — global mixer + device.
//   * `ma_sound[N]`      — preloaded sound bank.
//   * `Subscription`     — typed `AudioPlay` listener.
//
// AudioPlay events arrive on the event-channel drain thread (the sim
// thread, after `engine.step()` waves finish). We dispatch via the
// `ma_engine_play_sound_ex` family which spins up an internal voice
// per call; miniaudio guarantees thread-safety for that path.
//
// Pre-loaded WAVs are kept as full `ma_sound` instances and PLAYED via
// `ma_sound_seek_to_pcm_frame(0)` + `ma_sound_start` — this avoids
// re-decoding per shot, which is the right shape for short SFX (200ms
// laser zaps, hit cues, etc.). For larger sounds (music) we'd switch
// to a streaming flag, but M4.8 doesn't ship music — the original
// TOU's per-level OGGs are imported but the music driver is a v1.3
// follow-up.

#include "AudioSystem.hpp"

#include "DemoTypes.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

// miniaudio's single-header impl. Define ONCE in this TU.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING_BACKEND_FLAC
#define MA_NO_DECODING_BACKEND_MP3
#define MA_NO_ENCODING
#include <miniaudio/miniaudio.h>

namespace tou2d {

namespace {

/// Hard-coded mapping from `audio::*` IDs to filenames in `sfx/`.
/// Names cherry-picked from the original TOU's SFX bank — see the
/// inventory in `TOU_PLAN.md § M4.8`. When a file isn't present at
/// load time the slot stays empty and that sound becomes a silent
/// no-op (rest of the bank still works).
struct SoundEntry { std::uint16_t id; const char* file; };
constexpr std::array<SoundEntry, audio::kSoundCount> kSoundBank = {{
    { audio::kSoundDumbfire,  "fire_r.wav"   },  // basic laser zap
    { audio::kSoundSpread,    "PHOT_R.WAV"   },  // photon / spread
    { audio::kSoundHit,       "hit1_m.wav"   },  // bullet hits ship
    { audio::kSoundExplode,   "exp1_m.wav"   },  // ship explosion
    { audio::kSoundTileBreak, "BRIC_R.WAV"   },  // brick / terrain break
}};

} // namespace

struct AudioSystem::Impl {
    bool                         engineOk = false;
    ma_engine                    engine{};
    std::array<ma_sound, audio::kSoundCount> sounds{};
    std::array<bool, audio::kSoundCount>     soundOk{};
    std::filesystem::path        assetDir;
    threadmaxx::Subscription     sub;
    /// M6.5 — typed `AudioVolumeChanged` listener. Host emits once at
    /// startup after loading settings.dat and on every Options→Audio
    /// knob cycle. Applies via ma_engine_set_volume (master) and
    /// per-sound multipliers (sfx) — music is no-op until a music
    /// driver lands.
    threadmaxx::Subscription     volSub;
    /// M6.5 — 0.0..1.0 sfx multiplier applied to every AudioPlay event.
    /// Defaults to 1.0; updated by `AudioVolumeChanged` listener.
    float                        sfxScale_ = 1.0f;

    Impl(threadmaxx::Engine* eng, std::filesystem::path dir)
        : assetDir(std::move(dir)) {
        // ---- 1. Init engine -----------------------------------------
        ma_engine_config cfg = ma_engine_config_init();
        if (ma_engine_init(&cfg, &engine) != MA_SUCCESS) {
            std::fprintf(stderr,
                "[audio] ma_engine_init failed; sound disabled\n");
            return;
        }
        engineOk = true;

        // ---- 2. Pre-load bank ---------------------------------------
        const std::filesystem::path sfxDir = assetDir / "sfx";
        for (const auto& entry : kSoundBank) {
            const std::filesystem::path p = sfxDir / entry.file;
            if (!std::filesystem::exists(p)) {
                std::fprintf(stderr,
                    "[audio] missing sfx %s; slot %u silenced\n",
                    p.string().c_str(),
                    static_cast<unsigned>(entry.id));
                continue;
            }
            const ma_uint32 flags =
                MA_SOUND_FLAG_DECODE   |   // pre-decode into RAM
                MA_SOUND_FLAG_NO_SPATIALIZATION;
            const std::string utf8 = p.string();
            const ma_result r = ma_sound_init_from_file(
                &engine, utf8.c_str(), flags,
                /*group=*/nullptr, /*fence=*/nullptr,
                &sounds[entry.id]);
            if (r != MA_SUCCESS) {
                std::fprintf(stderr,
                    "[audio] ma_sound_init_from_file(%s) -> %d\n",
                    utf8.c_str(), int(r));
                continue;
            }
            soundOk[entry.id] = true;
        }

        // ---- 3. Subscribe to AudioVolumeChanged events --------------
        // Host emits once at startup after loading settings.dat AND on
        // every Options→Audio knob cycle. Master is applied through the
        // global engine volume; sfx is folded into per-sound multipliers
        // alongside the per-event volume in the AudioPlay handler.
        volSub = eng->events<AudioVolumeChanged>().subscribeScoped(
            [this](const AudioVolumeChanged& ev) {
                if (!engineOk) return;
                const float master = static_cast<float>(ev.master) / 100.0f;
                ma_engine_set_volume(&engine, master);
                // sfx 0..100 → 0.0..1.0; folded into per-sound volume on
                // the next AudioPlay. We just stash it on the engine via
                // a static — simplest path for v1; a per-instance field
                // would be cleaner if the host ran multiple engines.
                sfxScale_ = static_cast<float>(ev.sfx) / 100.0f;
                // music: no-op until a music driver lands.
                (void)ev.music;
            });

        // ---- 4. Subscribe to AudioPlay events ----------------------
        // The handler runs on the sim thread during the event-channel
        // drain at end of each tick; safe to call into ma_sound APIs.
        sub = eng->events<AudioPlay>().subscribeScoped(
            [this](const AudioPlay& ev) {
                if (!engineOk) return;
                if (ev.soundId >= soundOk.size())   return;
                if (!soundOk[ev.soundId])            return;
                ma_sound& s = sounds[ev.soundId];
                // Volume: 0 means "use sfxScale_ alone". Nonzero scales
                // through the M6.5 sfx slider: per-event * sfxScale_,
                // both in 0..1. Master is global on the engine and
                // applies on top of this.
                const float perEvent =
                    (ev.volume > 0)
                        ? (static_cast<float>(ev.volume) / 255.0f)
                        : 1.0f;
                ma_sound_set_volume(&s, perEvent * sfxScale_);
                // Restart-from-beginning + play. Polyphony: this
                // simple path cuts off the previous instance of the
                // same sound — fine for short SFX (laser zaps are
                // 200ms and chain rapidly so re-trigger feels right).
                ma_sound_seek_to_pcm_frame(&s, 0);
                ma_sound_start(&s);
            });
    }

    ~Impl() {
        // Subscription auto-detaches first (handled by ~Subscription).
        for (std::size_t i = 0; i < sounds.size(); ++i) {
            if (soundOk[i]) {
                ma_sound_uninit(&sounds[i]);
                soundOk[i] = false;
            }
        }
        if (engineOk) {
            ma_engine_uninit(&engine);
            engineOk = false;
        }
    }
};

AudioSystem::AudioSystem(threadmaxx::Engine* engine,
                         std::filesystem::path assetDir)
    : impl_(std::make_unique<Impl>(engine, std::move(assetDir))) {}

AudioSystem::~AudioSystem() = default;

std::size_t AudioSystem::soundsLoaded() const noexcept {
    if (!impl_) return 0;
    std::size_t n = 0;
    for (bool ok : impl_->soundOk) if (ok) ++n;
    return n;
}

} // namespace tou2d
