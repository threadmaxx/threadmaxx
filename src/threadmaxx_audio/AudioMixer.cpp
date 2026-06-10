/// @file AudioMixer.cpp
/// @brief AU2 mixer implementation — voice playback, bus routing, master
/// fold-down. Hot path (`mix()`) is zero-alloc; everything that grows
/// (`addClip`, `createBus`) lives outside it.
///
/// The bus graph is two-level: a fixed master bus (slot 0) plus user buses
/// (slots 1..N). Voices route to any bus including master; in mix() user
/// buses fold into master with their gain/mute/solo applied, then master's
/// own gain/mute applies to the final output.
///
/// Voice → bus channel adaptation handles mono clip → stereo bus duplication;
/// stereo → stereo is 1:1; matching channel counts copy through; mismatched
/// counts down-mix to mono then duplicate.

#include "threadmaxx_audio/mixer.hpp"

#include "threadmaxx_audio/clip.hpp"
#include "threadmaxx_audio/detail/voice_allocator.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace threadmaxx::audio {

namespace {

inline float dbToLinear(float db) noexcept {
    return std::pow(10.0f, db * 0.05f);
}

struct ClipEntry {
    bool          alive      = false;
    std::uint32_t generation = 0;
    Clip          clip;
};

struct BusEntry {
    bool          alive      = false;
    std::uint32_t generation = 0;
    BusDesc       desc{};
};

SoundId encodeSoundId(std::uint32_t slot, std::uint32_t generation) noexcept {
    return SoundId{ (static_cast<std::uint64_t>(generation) << 32) | static_cast<std::uint64_t>(slot) };
}
bool decodeSoundId(SoundId id, std::uint32_t cap, std::uint32_t& slot, std::uint32_t& gen) noexcept {
    if (id.value == 0) return false;
    slot = static_cast<std::uint32_t>(id.value & 0xFFFFFFFFu);
    gen  = static_cast<std::uint32_t>(id.value >> 32);
    return slot < cap;
}

BusId encodeBusId(std::uint32_t slot, std::uint32_t generation) noexcept {
    return BusId{ (static_cast<std::uint64_t>(generation) << 32) | static_cast<std::uint64_t>(slot) };
}
bool decodeBusId(BusId id, std::uint32_t cap, std::uint32_t& slot, std::uint32_t& gen) noexcept {
    if (id.value == 0) return false;
    slot = static_cast<std::uint32_t>(id.value & 0xFFFFFFFFu);
    gen  = static_cast<std::uint32_t>(id.value >> 32);
    return slot < cap;
}

} // namespace

struct AudioMixer::Impl {
    std::unique_ptr<IAudioDevice> device;
    AudioMixerConfig              config{};
    AudioFormat                   format{};
    std::size_t                   bufferFrames = 0;
    bool                          initialized  = false;

    std::vector<ClipEntry>        clips;
    std::vector<BusEntry>         buses;

    detail::VoiceAllocator        voices;
    std::uint64_t                 tickCounter        = 0;
    std::uint32_t                 droppedVoicesTotal = 0;

    // bus 0 (master) accumulator is masterBuffer. Bus i (i>=1) lives at
    // busBuffer[(i - 1) * samplesPerBus .. ].
    std::vector<float>            masterBuffer;
    std::vector<float>            busBuffer;

    [[nodiscard]] std::size_t samplesPerBus() const noexcept {
        return bufferFrames * static_cast<std::size_t>(format.channels);
    }
};

AudioMixer::AudioMixer(std::unique_ptr<IAudioDevice> device, AudioMixerConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->device = std::move(device);
    impl_->config = config;
}

AudioMixer::~AudioMixer() {
    if (impl_ && impl_->initialized) shutdown();
}

AudioMixer::AudioMixer(AudioMixer&&) noexcept            = default;
AudioMixer& AudioMixer::operator=(AudioMixer&&) noexcept = default;

bool AudioMixer::initialize(const AudioFormat& format, std::size_t bufferFrames) {
    if (!impl_ || !impl_->device) return false;
    if (format.channels == 0 || bufferFrames == 0) return false;
    if (impl_->initialized) shutdown();

    if (!impl_->device->initialize(format, bufferFrames)) return false;
    impl_->format       = format;
    impl_->bufferFrames = bufferFrames;

    impl_->clips.assign(impl_->config.maxClips, ClipEntry{});
    impl_->buses.assign(impl_->config.maxBuses, BusEntry{});
    impl_->buses[0].alive      = true;
    impl_->buses[0].generation = 1;
    impl_->buses[0].desc       = BusDesc{};

    impl_->voices.initialize(impl_->config.maxVoices);
    impl_->tickCounter        = 0;
    impl_->droppedVoicesTotal = 0;

    const std::size_t spb = impl_->samplesPerBus();
    impl_->masterBuffer.assign(spb, 0.0f);
    impl_->busBuffer.assign(spb * (impl_->config.maxBuses > 0 ? impl_->config.maxBuses - 1u : 0u), 0.0f);

    impl_->initialized = true;
    return true;
}

void AudioMixer::shutdown() {
    if (!impl_ || !impl_->initialized) return;
    impl_->voices.shutdown();
    impl_->clips.clear();
    impl_->buses.clear();
    impl_->masterBuffer.clear();
    impl_->busBuffer.clear();
    impl_->device->shutdown();
    impl_->initialized = false;
    impl_->bufferFrames = 0;
    impl_->format = AudioFormat{};
}

bool AudioMixer::initialized() const noexcept { return impl_ && impl_->initialized; }
AudioFormat AudioMixer::format() const noexcept { return impl_ ? impl_->format : AudioFormat{}; }
std::size_t AudioMixer::bufferFrames() const noexcept { return impl_ ? impl_->bufferFrames : 0; }
IAudioDevice& AudioMixer::device() noexcept { return *impl_->device; }
const IAudioDevice& AudioMixer::device() const noexcept { return *impl_->device; }

SoundId AudioMixer::addClip(std::span<const float> samples, AudioFormat format) {
    if (!impl_) return SoundId{0};
    for (std::uint32_t i = 0; i < impl_->clips.size(); ++i) {
        if (!impl_->clips[i].alive) {
            ++impl_->clips[i].generation;
            impl_->clips[i].alive        = true;
            impl_->clips[i].clip.format  = format;
            impl_->clips[i].clip.samples.assign(samples.begin(), samples.end());
            return encodeSoundId(i, impl_->clips[i].generation);
        }
    }
    return SoundId{0};
}

void AudioMixer::removeClip(SoundId id) {
    if (!impl_) return;
    std::uint32_t slot = 0, gen = 0;
    if (!decodeSoundId(id, static_cast<std::uint32_t>(impl_->clips.size()), slot, gen)) return;
    auto& e = impl_->clips[slot];
    if (!e.alive || e.generation != gen) return;
    e.alive = false;
    ++e.generation;
    e.clip.samples.clear();
    e.clip.format = AudioFormat{};
}

bool AudioMixer::isValidClip(SoundId id) const noexcept {
    if (!impl_) return false;
    std::uint32_t slot = 0, gen = 0;
    if (!decodeSoundId(id, static_cast<std::uint32_t>(impl_->clips.size()), slot, gen)) return false;
    const auto& e = impl_->clips[slot];
    return e.alive && e.generation == gen;
}

BusId AudioMixer::masterBus() const noexcept {
    if (!impl_ || impl_->buses.empty()) return BusId{0};
    return encodeBusId(0u, impl_->buses[0].generation);
}

BusId AudioMixer::createBus(const BusDesc& desc) {
    if (!impl_) return BusId{0};
    for (std::uint32_t i = 1; i < impl_->buses.size(); ++i) {
        if (!impl_->buses[i].alive) {
            ++impl_->buses[i].generation;
            impl_->buses[i].alive = true;
            impl_->buses[i].desc  = desc;
            return encodeBusId(i, impl_->buses[i].generation);
        }
    }
    return masterBus();
}

void AudioMixer::destroyBus(BusId id) {
    if (!impl_) return;
    std::uint32_t slot = 0, gen = 0;
    if (!decodeBusId(id, static_cast<std::uint32_t>(impl_->buses.size()), slot, gen)) return;
    if (slot == 0) return; // can't destroy master
    auto& b = impl_->buses[slot];
    if (!b.alive || b.generation != gen) return;
    b.alive = false;
    ++b.generation;
    b.desc = BusDesc{};
}

void AudioMixer::setBusGain(BusId id, float gainDb) {
    if (!impl_) return;
    std::uint32_t slot = 0, gen = 0;
    if (!decodeBusId(id, static_cast<std::uint32_t>(impl_->buses.size()), slot, gen)) return;
    auto& b = impl_->buses[slot];
    if (!b.alive || b.generation != gen) return;
    b.desc.gainDb = gainDb;
}

void AudioMixer::setBusMute(BusId id, bool muted) {
    if (!impl_) return;
    std::uint32_t slot = 0, gen = 0;
    if (!decodeBusId(id, static_cast<std::uint32_t>(impl_->buses.size()), slot, gen)) return;
    auto& b = impl_->buses[slot];
    if (!b.alive || b.generation != gen) return;
    b.desc.muted = muted;
}

void AudioMixer::setBusSolo(BusId id, bool solo) {
    if (!impl_) return;
    std::uint32_t slot = 0, gen = 0;
    if (!decodeBusId(id, static_cast<std::uint32_t>(impl_->buses.size()), slot, gen)) return;
    if (slot == 0) return; // master can't be solo'd
    auto& b = impl_->buses[slot];
    if (!b.alive || b.generation != gen) return;
    b.desc.solo = solo;
}

bool AudioMixer::isValidBus(BusId id) const noexcept {
    if (!impl_) return false;
    std::uint32_t slot = 0, gen = 0;
    if (!decodeBusId(id, static_cast<std::uint32_t>(impl_->buses.size()), slot, gen)) return false;
    const auto& b = impl_->buses[slot];
    return b.alive && b.generation == gen;
}

VoiceId AudioMixer::play(const VoiceDesc& desc) {
    if (!impl_ || !impl_->initialized) return VoiceId{0};
    if (!isValidClip(desc.sound)) return VoiceId{0};

    bool stolen = false;
    const std::uint32_t slotIdx = impl_->voices.allocate(impl_->tickCounter, stolen);
    if (stolen) ++impl_->droppedVoicesTotal;

    auto& s          = impl_->voices.slot(slotIdx);
    s.sound          = desc.sound;
    // Resolve bus: explicit BusId{0} or invalid bus falls back to master.
    s.bus            = (desc.bus.value == 0 || !isValidBus(desc.bus)) ? masterBus() : desc.bus;
    s.gainDb         = desc.gainDb;
    s.looping        = desc.looping;
    s.playheadFrames = 0;

    ++impl_->tickCounter;
    return impl_->voices.encode(slotIdx);
}

void AudioMixer::stop(VoiceId voice) {
    if (!impl_) return;
    std::uint32_t slot = 0;
    if (!impl_->voices.decode(voice, slot)) return;
    impl_->voices.free(slot);
}

bool AudioMixer::isPlaying(VoiceId voice) const noexcept {
    if (!impl_) return false;
    std::uint32_t slot = 0;
    return impl_->voices.decode(voice, slot);
}

namespace {

// Mix `bufferFrames` of `clip` starting at `v.playheadFrames` into `dst`.
// Returns true when the voice should stop (clip exhausted, non-looping).
bool mixVoiceInto(detail::VoiceSlot& v, const Clip& clip,
                  float* dst, std::size_t bufferFrames,
                  std::uint8_t busChans) noexcept {
    const std::size_t clipFrames = clip.frames();
    const std::uint8_t clipChans = clip.format.channels;
    if (clipFrames == 0 || clipChans == 0 || busChans == 0) return true;

    const float linearGain = dbToLinear(v.gainDb);
    bool reachedEnd = false;

    for (std::size_t i = 0; i < bufferFrames; ++i) {
        std::uint64_t f = v.playheadFrames + i;
        if (f >= clipFrames) {
            if (v.looping) {
                f = f % clipFrames;
            } else {
                reachedEnd = true;
                break;
            }
        }
        const float* src = clip.samples.data() + f * clipChans;
        float* d         = dst + i * busChans;

        if (clipChans == busChans) {
            for (std::uint8_t c = 0; c < busChans; ++c) d[c] += src[c] * linearGain;
        } else if (clipChans == 1 && busChans >= 2) {
            const float s = src[0] * linearGain;
            for (std::uint8_t c = 0; c < busChans; ++c) d[c] += s;
        } else {
            // Down-mix clip to mono, then fan out.
            float mono = 0.0f;
            for (std::uint8_t c = 0; c < clipChans; ++c) mono += src[c];
            mono = (mono / static_cast<float>(clipChans)) * linearGain;
            for (std::uint8_t c = 0; c < busChans; ++c) d[c] += mono;
        }
    }

    v.playheadFrames += bufferFrames;
    if (!v.looping && v.playheadFrames >= clipFrames) reachedEnd = true;
    return reachedEnd;
}

} // namespace

void AudioMixer::mix() {
    if (!impl_ || !impl_->initialized) return;

    const std::size_t spb       = impl_->samplesPerBus();
    const std::uint8_t channels = impl_->format.channels;

    std::fill(impl_->masterBuffer.begin(), impl_->masterBuffer.end(), 0.0f);
    std::fill(impl_->busBuffer.begin(), impl_->busBuffer.end(), 0.0f);

    // Pass 1: voices → bus buffers.
    for (std::uint32_t i = 0; i < impl_->voices.capacity(); ++i) {
        auto& v = impl_->voices.slot(i);
        if (!v.alive) continue;

        std::uint32_t clipSlot = 0, clipGen = 0;
        if (!decodeSoundId(v.sound, static_cast<std::uint32_t>(impl_->clips.size()), clipSlot, clipGen)
            || !impl_->clips[clipSlot].alive
            || impl_->clips[clipSlot].generation != clipGen) {
            impl_->voices.free(i);
            continue;
        }
        const Clip& clip = impl_->clips[clipSlot].clip;

        std::uint32_t busSlot = 0, busGen = 0;
        if (!decodeBusId(v.bus, static_cast<std::uint32_t>(impl_->buses.size()), busSlot, busGen)
            || !impl_->buses[busSlot].alive
            || impl_->buses[busSlot].generation != busGen) {
            busSlot = 0; // fall back to master
        }

        float* dst = (busSlot == 0)
                     ? impl_->masterBuffer.data()
                     : impl_->busBuffer.data() + (static_cast<std::size_t>(busSlot) - 1u) * spb;

        const bool stop = mixVoiceInto(v, clip, dst, impl_->bufferFrames, channels);
        if (stop) impl_->voices.free(i);
    }

    // Pass 2: detect any solo'd non-master bus.
    bool anySolo = false;
    for (std::uint32_t i = 1; i < impl_->buses.size(); ++i) {
        if (impl_->buses[i].alive && impl_->buses[i].desc.solo) { anySolo = true; break; }
    }

    // Pass 3: fold non-master buses into master with gain/mute/solo.
    for (std::uint32_t i = 1; i < impl_->buses.size(); ++i) {
        const auto& b = impl_->buses[i];
        if (!b.alive) continue;
        if (b.desc.muted) continue;
        if (anySolo && !b.desc.solo) continue;

        const float gain  = dbToLinear(b.desc.gainDb);
        const float* src  = impl_->busBuffer.data() + (static_cast<std::size_t>(i) - 1u) * spb;
        for (std::size_t s = 0; s < spb; ++s) {
            impl_->masterBuffer[s] += src[s] * gain;
        }
    }

    // Pass 4: master gain/mute.
    const auto& master = impl_->buses[0];
    if (master.desc.muted) {
        std::fill(impl_->masterBuffer.begin(), impl_->masterBuffer.end(), 0.0f);
    } else {
        const float mgain = dbToLinear(master.desc.gainDb);
        if (mgain != 1.0f) {
            for (auto& s : impl_->masterBuffer) s *= mgain;
        }
    }

    impl_->device->submit(ConstAudioSpan{ impl_->masterBuffer.data(), impl_->bufferFrames, impl_->format });
}

MixerStats AudioMixer::stats() const noexcept {
    MixerStats out{};
    if (!impl_) return out;
    out.activeVoices    = impl_->voices.activeCount();
    out.allocatedVoices = impl_->voices.capacity();
    out.droppedVoices   = impl_->droppedVoicesTotal;
    out.underruns       = 0;
    return out;
}

void AudioMixer::resetPeaks() noexcept {
    // AU6 wires the meters; AU2 stub keeps the public symbol available.
}

} // namespace threadmaxx::audio
