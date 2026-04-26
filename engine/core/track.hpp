#pragma once

#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/audio/midi_input.hpp"
#include "engine/audio/sound.hpp"
#include "engine/core/clip.hpp"
#include "engine/core/modulator.hpp"
#include "engine/plugin/iplugin.hpp"
#include "engine/plugin/plugin_proxy.hpp"
#include "engine/vst3/vst3_host.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

// struct DummyMutex {
//   void lock() {}
//   void unlock() {}
// };
using DummyMutex = std::mutex;

struct TimelineClip {
  std::unique_ptr<Clip> clip;
  double start_time_sec = 0.0;
  double duration_sec = 0.0;      // Duration in seconds (for audio clips)
  double duration_beats = 0.0;    // Duration in beats (for MIDI clips)
  double trim_start_beats = 0.0;  // Head-trim offset into clip content
  int alias_source = -1;  // Source clip index for aliases (-1 = not alias)
  double loop_interval_beats = 0.0;  // Loop repeat period in beats (0 = full content)
};

// Automation lane targeting a single plugin parameter
struct AutomationLane {
  int plugin_idx = 0;
  uint32_t param_id = 0;
  std::vector<std::unique_ptr<TimelineClip>> clips;
};

// Tension-based interpolation between two automation points
// tension=0 → linear, >0 → ease-in (slow start), <0 → ease-out (fast start)
inline float InterpolateAutomation(float v0, float v1, float t, float tension) {
  if (t <= 0.0f) return v0;
  if (t >= 1.0f) return v1;
  // Apply tension curve: t' = t^(2^tension)
  // tension=0 → t^1 (linear), tension=1 → t^2 (ease-in), tension=-1 → t^0.5
  // (ease-out)
  float exponent = std::pow(2.0f, tension);
  float curved_t = std::pow(t, exponent);
  return v0 + (v1 - v0) * curved_t;
}

// Get the automation value at a given time in beats
inline float GetAutomationValue(const AutomationLane& lane, double time_beats,
                                double bpm) {
  if (lane.clips.empty()) return 0.0f;

  const TimelineClip* prev_clip = nullptr;
  const TimelineClip* active_clip = nullptr;

  for (const auto& tc : lane.clips) {
    if (!tc || !tc->clip || tc->clip->type != Clip::AUTOMATION) continue;

    double clip_start_beats = tc->start_time_sec * (bpm / 60.0);
    double clip_end_beats = clip_start_beats + tc->duration_beats;

    if (time_beats >= clip_start_beats && time_beats < clip_end_beats) {
      active_clip = tc.get();
      break;
    } else if (time_beats >= clip_end_beats) {
      if (!prev_clip || tc->start_time_sec > prev_clip->start_time_sec) {
        prev_clip = tc.get();
      }
    }
  }

  if (active_clip) {
    const auto& points = active_clip->clip->automation_points;
    if (points.empty()) return 0.0f;
    double local_beats =
        time_beats - (active_clip->start_time_sec * (bpm / 60.0));

    if (points.size() == 1) return points[0].value();
    if (local_beats <= points.front().time_beats())
      return points.front().value();
    if (local_beats >= points.back().time_beats()) return points.back().value();

    // Binary search for the segment containing local_beats
    size_t lo = 0, hi = points.size() - 1;
    while (lo + 1 < hi) {
      size_t mid = (lo + hi) / 2;
      if (points[mid].time_beats() <= local_beats)
        lo = mid;
      else
        hi = mid;
    }
    const auto& p0 = points[lo];
    const auto& p1 = points[hi];
    float segment_t = (float)((local_beats - p0.time_beats()) /
                              (p1.time_beats() - p0.time_beats()));
    return InterpolateAutomation(p0.value(), p1.value(), segment_t,
                                 p0.tension());
  } else if (prev_clip) {
    const auto& points = prev_clip->clip->automation_points;
    if (points.empty()) return 0.0f;
    return points.back().value();
  } else {
    // Find the earliest clip's first point
    float first_val = 0.0f;
    double min_start = 1e9;
    for (const auto& tc : lane.clips) {
      if (!tc || !tc->clip || tc->clip->type != Clip::AUTOMATION) continue;
      if (!tc->clip->automation_points.empty() &&
          tc->start_time_sec < min_start) {
        min_start = tc->start_time_sec;
        first_val = tc->clip->automation_points.front().value();
      }
    }
    return first_val;
  }
}

class Track {
 public:
  DummyMutex mutex;
  int index;
  std::string name;  // User-defined track name
  std::vector<std::unique_ptr<IPlugin>> plugins;
  std::map<int, std::unique_ptr<Clip>> clips;
  std::vector<std::unique_ptr<TimelineClip>> timeline_clips;
  std::vector<AutomationLane> automation_lanes;
  std::map<int, PluginModulation> modulations;  // key = plugin_idx

  // Sidechain routing: per-plugin source track index (-1 = no sidechain)
  struct SidechainRoute {
    int source_track_index = -1;  // -1 = disabled
  };
  std::map<int, SidechainRoute> plugin_sidechain;  // key = plugin_index
  std::map<int, bool> plugin_bypass;  // key = plugin_index, true = bypassed

  int playing_slot = -1;
  double current_time_sec = 0.0;
  int current_midi_idx = 0;
  std::atomic<bool> panic_requested_{false};

  Track(int idx) : index(idx) {}

  // Mixer state
  float volume = 0.31623f;  // linear gain; default = -10 dB
  float pan = 0.0f;  // -1.0 (full left) to 1.0 (full right), 0.0 = center
  bool muted = false;
  bool soloed = false;

  // Recording state
  enum class RecordMode { RECORD_AUDIO = 0, RECORD_MIDI = 1 };
  RecordMode record_mode =
      RecordMode::RECORD_MIDI;  // Default to MIDI for new tracks
  bool record_armed = false;
  std::string input_device_id;                // Selected input device
  int input_channel_start = 0;                // 0-based start channel
  bool input_stereo = true;                   // true = stereo, false = mono
  std::unique_ptr<SoundDevice> input_device;  // Lazily created on record start
  std::vector<float> record_buffer;           // Accumulates captured audio

  // MIDI recording buffer
  struct TimestampedMidiEvent {
    double time_sec;  // Absolute time from record start
    MidiNoteEvent event;
  };
  std::vector<TimestampedMidiEvent> midi_record_buffer;

  // MIDI input state
  std::string midi_input_device_id = MIDI_GLOBAL_ID;  // Default: global
  std::unique_ptr<MidiInput> midi_input_device;       // Lazily created

  // Virtual MIDI queue (from PC keyboard / on-screen piano)
  std::mutex virtual_midi_mutex;
  std::vector<MidiNoteEvent> virtual_midi_queue;

  struct LoadResult {
    int index = -1;                      // slot index, -1 on failure
    std::unique_ptr<IPlugin> displaced;  // old plugin displaced by replacement
  };
  LoadResult LoadPlugin(const std::string& path, int plugin_index,
                        double sample_rate,
                        PluginHostMode host_mode = PluginHostMode::IN_PROCESS,
                        const std::string& remote_host = "");
  bool DeleteClip(int slot);
  bool LoadClip(int slot, const std::string& path, bool is_loop = false,
                double sample_rate = 0.0);
  void SetClipLoop(int slot, bool is_loop);
  void PlayClip(int slot);
  void Stop();
  void Panic();
  std::unique_ptr<IPlugin> RemovePlugin(size_t pidx);
  void ReorderPlugin(int from_index, int to_index);

  void AddTimelineClip(const std::string& path, double start_time_sec,
                       double bpm, double duration_beats = 0,
                       double sample_rate = 0.0);
  void RemoveTimelineClip(int clip_index);

  int AddAutomationLane(int plugin_idx, uint32_t param_id);
  bool RemoveAutomationLane(int lane_index);
};

}  // namespace hibiki
