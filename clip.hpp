#pragma once

#include <expected>
#include <vector>
#include <string>
#include <memory>
#include "midi.hpp"

namespace hibiki {

struct Clip {
    enum Type { MIDI, AUDIO } type;
    std::vector<hibiki::MidiEvent> midi_events;
    std::vector<float> audio_data;
    int num_channels = 0;
    double sample_rate = 0.0;
    double duration_sec = 0.0;
    std::vector<float> waveform_summary;
    std::string path;
    bool is_loop = false;
};

std::unique_ptr<Clip> LoadClip(const std::string& path, bool is_loop = false);
std::expected<Clip, std::string> MaybeLoadClip(const std::string& path, bool is_loop = false);

} // namespace hibiki
