#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hibiki {

struct MidiEvent {
  double beats;  // Time in quarter notes (beats), not seconds
  uint8_t type;
  uint8_t channel;
  uint8_t note;
  uint8_t velocity;
};

bool isNoteOn(const MidiEvent& ev);

bool isNoteOff(const MidiEvent& ev);

std::vector<MidiEvent> parseMidi(const std::string& path);

}  // namespace hibiki
