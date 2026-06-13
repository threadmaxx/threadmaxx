/// @file panels/AudioPanel.cpp
/// @brief ST16 — `AudioPanel` implementation.

#include <threadmaxx_studio/panels/audio.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_audio/diagnostics.hpp>
#include <threadmaxx_audio/mixer.hpp>

#include <cstdio>

namespace threadmaxx::studio {

AudioPanel::AudioPanel(audio::AudioMixer& mixer) noexcept
    : mixer_(&mixer) {}

void AudioPanel::render(editor::IEditorBackend& backend,
                        IStudioDataSource&) {
    if (mixer_ == nullptr) {
        backend.drawText("Audio: <detached>", 0.0f, 0.0f);
        lastBuses_ = 0;
        return;
    }

    audio::AudioDiagnostics diag{*mixer_};
    const auto stats = diag.stats();
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Audio  voices=%u/%u  drops=%u  underruns=%u  "
                  "peakL=%.2f  peakR=%.2f",
                  stats.activeVoices, stats.allocatedVoices,
                  stats.droppedVoices, stats.underruns,
                  static_cast<double>(stats.peakL),
                  static_cast<double>(stats.peakR));
    backend.drawText(buf, 0.0f, 0.0f);

    const auto buses = diag.listBuses();
    lastBuses_ = buses.size();
    float y = 16.0f;
    for (const auto& b : buses) {
        char row[128];
        std::snprintf(row, sizeof(row),
                      "bus#%llu  %s  gain=%.1fdB  %s%s  voices=%u",
                      static_cast<unsigned long long>(b.id.value),
                      b.isMaster ? "[master]" : "[normal]",
                      static_cast<double>(b.gainDb),
                      b.muted ? "M " : "  ",
                      b.solo  ? "S " : "  ",
                      b.voiceCount);
        backend.drawText(row, 0.0f, y);
        y += 14.0f;
    }
}

} // namespace threadmaxx::studio
