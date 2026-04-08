#include "engine/core/clip.hpp"

#include <algorithm>
#include <cmath>

#include "engine/core/audio_file.hpp"

namespace hibiki {
/*
std::unique_ptr<Clip> LoadClip(const std::string& path, bool is_loop) {
    auto clip = MaybeLoadClip(path, is_loop);
    if (clip) {
        auto ret = std::make_unique<Clip>();
        *ret = *clip;
        return ret;
    }
    return nullptr;
}

std::expected<Clip, std::string> MaybeLoadClip(const std::string& path, bool
is_loop) {
*/
std::unique_ptr<Clip> LoadClip(const std::string& path, bool is_loop) {
  Clip clip;
  clip.path = path;
  clip.is_loop = is_loop;

  if (path.size() > 4 && path.substr(path.size() - 4) == ".wav") {
    if (!LoadWav(path, clip.audio_data, clip.num_channels, clip.duration_sec)) {
      return nullptr;  // std::unexpected("Cannot load wav: " + path);
    }
    clip.type = Clip::Type::AUDIO;

    // Generate waveform summary for AUDIO clips
    if (!clip.audio_data.empty()) {
      int num_points = 256;
      clip.waveform_summary.resize(num_points);
      int samples_per_point =
          clip.audio_data.size() / (clip.num_channels * num_points);
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
    // Do not return nullptr even if empty, to allow dropping empty/invalid MIDI
    // clips
    clip.midi_events = std::move(events);
    clip.type = Clip::Type::MIDI;
    // For MIDI clips, set duration_beats (in quarter notes)
    // duration_sec remains 0 - GUI can check which is non-zero
    if (!clip.midi_events.empty()) {
      clip.duration_beats =
          clip.midi_events.back().beats + 0.1;  // +0.1 beats buffer
    } else {
      clip.duration_beats = 4.0;  // Default duration of 4 beats if no events
    }

    // Encode midi events into waveform_summary: [start_ratio, pitch,
    // duration_ratio] Values are normalized 0-1 relative to clip duration in
    // beats
    double total_beats = clip.duration_beats > 0 ? clip.duration_beats : 1.0;
    for (size_t i = 0; i < clip.midi_events.size(); ++i) {
      auto& ev = clip.midi_events[i];
      if (hibiki::isNoteOn(ev)) {
        double duration = 0.1;  // Default short duration if no NoteOff found
        // Search for corresponding NoteOff
        for (size_t j = i + 1; j < clip.midi_events.size(); ++j) {
          auto& off_ev = clip.midi_events[j];
          if (off_ev.note == ev.note && off_ev.channel == ev.channel &&
              hibiki::isNoteOff(off_ev)) {
            duration = off_ev.beats - ev.beats;
            break;
          }
        }
        // Store as ratios (0-1) so GUI doesn't need BPM conversion
        clip.waveform_summary.push_back((float)(ev.beats / total_beats));
        clip.waveform_summary.push_back((float)ev.note);
        clip.waveform_summary.push_back((float)(duration / total_beats));
      }
    }
  }

  return std::make_unique<Clip>(clip);
}

}  // namespace hibiki
