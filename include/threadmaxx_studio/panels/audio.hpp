#pragma once

/// @file panels/audio.hpp
/// @brief ST16 — `AudioPanel` renders a bus table for an
/// `audio::AudioMixer`. Reads through `audio::AudioDiagnostics`
/// (AU9 surface): MixerStats header + one row per bus.

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::audio {
class AudioMixer;
} // namespace threadmaxx::audio

namespace threadmaxx::studio {

class AudioPanel : public IStudioPanel {
public:
    AudioPanel() noexcept = default;
    explicit AudioPanel(audio::AudioMixer& mixer) noexcept;

    void setMixer(audio::AudioMixer* mixer) noexcept { mixer_ = mixer; }
    [[nodiscard]] audio::AudioMixer* mixer() const noexcept { return mixer_; }

    std::string_view id() const noexcept override { return "sibling.audio"; }
    std::string_view title() const noexcept override { return "Audio"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Bus row count from the most recent render.
    [[nodiscard]] std::size_t busRowCount() const noexcept { return lastBuses_; }

private:
    audio::AudioMixer* mixer_{nullptr};
    std::size_t        lastBuses_{0};
};

} // namespace threadmaxx::studio
