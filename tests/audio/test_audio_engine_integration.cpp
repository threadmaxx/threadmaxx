// AU7 — recommended ISystem integration pattern. Spawns a moving emitter
// entity, registers an `AudioSystem` that reads the entity's Transform each
// tick and feeds it into the mixer via the scene.hpp helpers; we verify the
// captured audio reflects the per-tick position by checking that
// "near the listener" mixes loud and "far from the listener" mixes silent.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace {

float peakAbs(const std::vector<float>& buf) {
    float peak = 0.0f;
    for (float v : buf) {
        const float a = v < 0.0f ? -v : v;
        if (a > peak) peak = a;
    }
    return peak;
}

// Recommended integration pattern: an ISystem that holds a back-reference
// to the AudioMixer + a list of (entity, voice) pairs, reads entity
// Transforms in postStep(), and calls mixer.mix() in the same hook. The
// library stays engine-agnostic; this class lives in the consumer.
class AudioSystem final : public threadmaxx::ISystem {
public:
    AudioSystem(threadmaxx::audio::AudioMixer& mixer,
                threadmaxx::audio::ListenerId listener) noexcept
        : mixer_(&mixer), listener_(listener) {}

    void track(threadmaxx::EntityHandle entity,
               threadmaxx::audio::VoiceId voice) {
        emitters_.emplace_back(entity, voice);
    }

    const char* name() const noexcept override { return "AudioSystem"; }

    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update(threadmaxx::SystemContext&) override {
        // Reading is done in postStep so we see the latest committed
        // Transform from this tick's writers (e.g. the TeleportSystem
        // running earlier in the registration order).
    }

    void postStep(threadmaxx::SystemContext& ctx) override {
        const threadmaxx::World* world = ctx.worldView().world();
        if (world) {
            for (auto& [entity, voice] : emitters_) {
                const auto* t = world->tryGetTransform(entity);
                if (!t) continue;
                const threadmaxx::audio::Vec3 pos{
                    t->position.x, t->position.y, t->position.z };
                threadmaxx::audio::setEmitterPose(
                    *mixer_, voice, listener_,
                    pos,
                    /*velocity*/ { 0.0f, 0.0f, 0.0f },
                    /*minDistance*/ 1.0f,
                    /*maxDistance*/ 10.0f,
                    /*dopplerFactor*/ 0.0f,
                    threadmaxx::audio::AttenuationModel::Linear);
            }
        }
        mixer_->mix();
    }

private:
    threadmaxx::audio::AudioMixer*                                                       mixer_;
    threadmaxx::audio::ListenerId                                                        listener_;
    std::vector<std::pair<threadmaxx::EntityHandle, threadmaxx::audio::VoiceId>>         emitters_;
};

class TeleportSystem final : public threadmaxx::ISystem {
public:
    TeleportSystem(threadmaxx::EntityHandle target, threadmaxx::Vec3 dest)
        : target_(target), dest_(dest) {}
    const char* name() const noexcept override { return "Teleport"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    void update(threadmaxx::SystemContext& ctx) override {
        if (fired_) return;
        fired_ = true;
        ctx.single([this](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
            cb.setTransform(target_, threadmaxx::Transform{ dest_, {}, threadmaxx::Vec3{1,1,1} });
        });
    }
private:
    threadmaxx::EntityHandle target_;
    threadmaxx::Vec3         dest_;
    bool                     fired_ = false;
};

} // namespace

int main() {
    using namespace threadmaxx::audio;

    // ---- Audio side: mixer + listener at origin facing +Z.
    auto deviceOwner    = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();
    AudioMixer mixer(std::move(deviceOwner));
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    CHECK(mixer.initialize(fmt, 256));

    ListenerDesc l{};
    l.position = { 0.0f, 0.0f, 0.0f };
    l.forward  = { 0.0f, 0.0f, 1.0f };
    l.up       = { 0.0f, 1.0f, 0.0f };
    ListenerId lid = mixer.createListener(l);

    std::vector<float> dc(256 * 2, 0.5f);
    SoundId clip = mixer.addClip(dc, fmt);
    VoiceId voice = mixer.play(VoiceDesc{ .sound = clip, .looping = true });

    // ---- Engine side: spawn one entity at near-listener position.
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    threadmaxx::Engine engine(cfg);

    threadmaxx::EntityHandle emitter{};
    struct Game : threadmaxx::IGame {
        threadmaxx::EntityHandle& out;
        explicit Game(threadmaxx::EntityHandle& h) : out(h) {}
        void onSetup(threadmaxx::Engine& eng,
                     threadmaxx::World&,
                     threadmaxx::CommandBuffer& seed) override {
            out = eng.reserveEntityHandle();
            seed.spawn(out,
                       threadmaxx::Transform{ threadmaxx::Vec3{0.0f, 0.0f, 1.0f}, {}, threadmaxx::Vec3{1,1,1} },
                       {}, {}, {}, {}, {},
                       threadmaxx::ComponentSet{threadmaxx::Component::Transform});
        }
    };
    Game game(emitter);
    CHECK(engine.initialize(game));

    auto audioSystem      = std::make_unique<AudioSystem>(mixer, lid);
    AudioSystem* asysRaw  = audioSystem.get();
    asysRaw->track(emitter, voice);
    engine.registerSystem(std::move(audioSystem));

    // Tick once: AudioSystem reads (0, 0, 1) — inside minDistance → audible.
    engine.step();
    CHECK(!dev->capturedBuffers().empty());
    const float peakNear = peakAbs(dev->capturedBuffers().back());
    CHECK(peakNear > 0.2f);

    // Move the entity past maxDistance. TeleportSystem writes Transform
    // earlier in the registration order; AudioSystem's postStep sees the
    // committed update on the same tick.
    engine.registerSystem(std::make_unique<TeleportSystem>(emitter,
        threadmaxx::Vec3{0.0f, 0.0f, 200.0f}));

    dev->clearCaptured();
    engine.step();
    CHECK(!dev->capturedBuffers().empty());
    const float peakFar = peakAbs(dev->capturedBuffers().back());
    CHECK(peakFar < peakNear * 0.2f);

    EXIT_WITH_RESULT();
}
