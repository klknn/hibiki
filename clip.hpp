#pragma once

#include <vector>
#include <string>
#include "midi.hpp"

struct Clip {
    enum Type { MIDI, AUDIO } type;
    std::vector<hbk::MidiEvent> midi_events;
    std::vector<float> audio_data; // Mono or interleaved stereo, but we'll assume stereo for simplicity or handle both
    int num_channels = 0;
    double duration_sec = 0.0;
    std::string path;
    bool is_loop = false;
    std::vector<float> waveform_summary;

    Clip() = default;
};
