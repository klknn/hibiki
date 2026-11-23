#include "engine/core/midi.hpp"

#include <fstream>
#include <vector>

namespace hibiki {

extern int d_parse_midi_bytes(const uint8_t* buf, size_t len,
                              MidiEvent* outEvents, int maxEvents);

std::vector<MidiEvent> parseMidi(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return {};
  std::streamsize size = file.tellg();
  if (size <= 0) return {};
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> raw(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(raw.data()), size)) return {};

  int count = d_parse_midi_bytes(raw.data(), raw.size(), nullptr, 0);
  if (count <= 0) return {};

  std::vector<MidiEvent> events(static_cast<size_t>(count));
  d_parse_midi_bytes(raw.data(), raw.size(), events.data(), count);
  return events;
}

}  // namespace hibiki
