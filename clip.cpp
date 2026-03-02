#include "clip.hpp"
#include "audio_file.hpp"
#include <cmath>
#include <algorithm>

namespace hibiki {

std::unique_ptr<Clip> LoadClip(const std::string& path, bool is_loop) {
    auto clip = MaybeLoadClip(path, is_loop);
    if (clip) {
        auto ret = std::make_unique<Clip>();
        *ret = *clip;
        return ret;
    }
    return nullptr;
}

std::expected<Clip, std::string> MaybeLoadClip(const std::string& path, bool is_loop) {
    Clip clip;
    clip.path = path;
    clip.is_loop = is_loop;

    if (path.size() > 4 && path.substr(path.size() - 4) == ".wav") {
        if (!LoadWav(path, clip.audio_data, clip.num_channels, clip.duration_sec)) {
            return std::unexpected("Cannot load wav: " + path);
        }
        clip.type = Clip::Type::AUDIO;

        // Generate waveform summary for AUDIO clips
        if (!clip.audio_data.empty()) {
            int num_points = 256;
            clip.waveform_summary.resize(num_points);
            int samples_per_point = clip.audio_data.size() / (clip.num_channels * num_points);
            if (samples_per_point < 1) samples_per_point = 1;

            for (int i = 0; i < num_points; i++) {
                float max_val = 0;
                for (int j = 0; j < samples_per_point; j++) {
                    int idx = (i * samples_per_point + j) * clip.num_channels;
                    if (idx < (int)clip.audio_data.size()) {
                        max_val = std::max(max_val, std::abs(clip.audio_data[idx]));
                    }
                }
                clip.waveform_summary[i] = max_val;
            }
        }
    } else {
        auto events = hibiki::parseMidi(path);
        if (events.empty()) {
            return std::unexpected("Failed to load or empty midi: " + path);
        }
        clip.midi_events = std::move(events);
        clip.type = Clip::Type::MIDI;
        if (!clip.midi_events.empty()) {
            clip.duration_sec = clip.midi_events.back().seconds + 0.1; // Small buffer
        }
    }

    return clip;
}

} // namespace hibiki
