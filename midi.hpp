#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace hbk {

struct MidiEvent {
    double seconds;
    uint8_t type;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
};

bool isNoteOn(const MidiEvent& ev);

bool isNoteOff(const MidiEvent& ev);

std::vector<MidiEvent> parseMidi(const std::string& path);

} // namespace hbk
