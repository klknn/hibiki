// Empty stub sequencer-based MIDI input implementation.
// Uses snd_seq for device enumeration and event reading.

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "engine/audio/midi_input.hpp"

namespace hibiki {

class MidiInputStub : public MidiInput {
 public:
  ~MidiInputStub() override { close(); }

  bool open(const std::string& device_id) override { return false; }

  std::vector<MidiNoteEvent> read() override { return {}; }

  void close() override {}
};

std::unique_ptr<MidiInput> MidiInput::create() { return nullptr; }

std::vector<MidiInputInfo> MidiInput::listDevices() { return {}; }

}  // namespace hibiki
