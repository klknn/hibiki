#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Special device ID that means "listen to all MIDI inputs"
static constexpr const char* MIDI_GLOBAL_ID = "__global__";

struct MidiInputInfo {
  std::string id;
  std::string name;
  int port_count = 0;
};

// Abstract MIDI input reader.
class MidiInput {
 public:
  virtual ~MidiInput() = default;

  // Open a MIDI input device by ID. "__global__" subscribes to all.
  virtual bool open(const std::string& device_id) = 0;

  // Non-blocking read of pending MIDI events.
  // Returns events with sampleOffset = 0 (real-time, no lookahead).
  virtual std::vector<MidiNoteEvent> read() = 0;

  // Close the device.
  virtual void close() = 0;

  // Factory: create platform-specific MidiInput.
  static std::unique_ptr<MidiInput> create();

  // Enumerate available MIDI input devices.
  // First entry is always the Global ("All Inputs") device.
  static std::vector<MidiInputInfo> listDevices();
};

}  // namespace hibiki
