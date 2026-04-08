#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/core/midi.hpp"
#include "pb/core.pb.h"

namespace hibiki {

struct Clip {
  enum Type { MIDI, AUDIO, AUTOMATION } type;
  std::vector<hibiki::MidiEvent> midi_events;
  std::vector<float> audio_data;
  int num_channels = 0;
  double sample_rate = 0.0;
  double duration_sec = 0.0;  // Duration in seconds (for AUDIO clips)
  double duration_beats =
      0.0;  // Duration in beats/quarter notes (for MIDI/AUTOMATION clips)
  std::vector<float> waveform_summary;
  std::string path;
  std::string name;
  bool is_loop = false;
  std::vector<hibiki::pb::core::AutomationPoint> automation_points;
};

std::unique_ptr<Clip> LoadClip(const std::string& path, bool is_loop = false);
// std::expected<Clip, std::string> MaybeLoadClip(const std::string& path, bool
// is_loop = false);

}  // namespace hibiki
