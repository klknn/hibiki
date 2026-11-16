#pragma once

#include <limits>
#include <map>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "engine/core/track.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

enum class SequencerPlaybackMode { PAT = 0, SONG = 1 };

struct ChannelPatternStepData {
  std::vector<bool> steps = std::vector<bool>(16, false);
  std::vector<float> step_velocities = std::vector<float>(16, 0.78f);
  std::vector<float> step_pitches = std::vector<float>(16, 0.0f);
  std::vector<float> step_pans = std::vector<float>(16, 0.0f);
  std::vector<float> step_shifts = std::vector<float>(16, 0.0f);
  std::vector<hibiki::pb::core::MidiEvent> notes;
};

struct SequencerPattern {
  int pattern_index = 0;
  std::string name;
  uint32_t color = 0x00385770;
  int length_steps = 16;
  std::map<int, ChannelPatternStepData> channels;
};

struct SequencerChannel {
  int channel_index = 0;
  std::string name;
  std::string uri;
  std::string category = "Synths";
  uint32_t color = 0x004A6A8A;
  int mixer_insert = 1;
  float volume = 0.78f;
  float pan = 0.0f;
  bool muted = false;
  bool soloed = false;
};

struct ProjectState {
  std::map<int, std::unique_ptr<Track>> tracks;
  double bpm =
      std::numeric_limits<double>::quiet_NaN();  // NaN to catch uninitialized
                                                 // BPM bugs
  bool is_playing = false;
  double sample_rate =
      std::numeric_limits<double>::quiet_NaN();  // NaN to catch uninitialized
                                                 // sample_rate bugs

  double playhead_pos_sec = 0.0;
  bool is_timeline_playing = false;
  std::vector<float> levels = {0.0f, 0.0f};

  std::map<int, std::pair<float, float>> track_levels;
  std::mutex tracks_mutex;
  std::mutex levels_mutex;
  bool quit = false;

  // Plugin hosting mode (set via SetPluginHostMode command)
  PluginHostMode plugin_host_mode = PluginHostMode::IN_PROCESS;
  std::vector<std::string> remote_hosts;  // ["host:port", ...] for REMOTE mode
  int buffer_latency_ms = 200;  // Audio buffer latency in ms (configurable)
  bool use_double_precision = false;  // 64-bit float engine processing

  // Recording state
  bool is_recording = false;
  double record_start_sec = 0.0;  // Playhead position when recording started
  std::string project_dir;        // "" = unsaved, set on save/load

  // Loop region
  bool loop_enabled = false;
  double loop_start_sec = 0.0;
  double loop_end_sec = 0.0;

  // Global transport/mixer/telemetry state
  bool metronome_enabled = false;
  float master_volume = 0.82f;
  float master_pitch = 0.0f;
  int cpu_percent = 0;
  std::vector<float> oscilloscope;

  // Channel Rack & Pattern Sequencer state
  SequencerPlaybackMode playback_mode = SequencerPlaybackMode::PAT;
  float swing_amount = 0.0f;
  int selected_pattern_index = 0;
  std::vector<SequencerChannel> sequencer_channels;
  std::vector<SequencerPattern> sequencer_patterns;
};

// Convert a duration in beats to seconds at the given BPM.
double beatsToSec(double beats, double bpm);

// Returns a pointer to the track, creating it if it doesn't exist
Track* GetOrCreateTrack(ProjectState& state, int track_index);

// Collect MIDI NoteOn/NoteOff events for `ch_idx` inside `pat` over the beat
// window `[win_start_beats, win_start_beats + block_dur_beats)` wrapped at
// `loop_beats` (if `wrap_loop == true`).
void CollectPatternBlockMidiEvents(const SequencerPattern& pat, int ch_idx,
                                   float swing_amount, double loop_beats,
                                   double win_start_beats,
                                   double block_dur_beats, double bpm,
                                   float sample_rate, int block_size,
                                   bool wrap_loop,
                                   std::vector<MidiNoteEvent>& out);

/// Collects SONG-mode pattern clip MIDI events for generator channel `ch_idx`
/// across `[block_start_sec, block_start_sec + time_per_block)`, clipping
/// sample offsets cleanly and emitting NoteOffs when a pattern clip ends.
void CollectSongPatternClipBlockEvents(const ProjectState& state, int ch_idx,
                                       double block_start_sec,
                                       double time_per_block, float sample_rate,
                                       int block_size,
                                       std::vector<MidiNoteEvent>& out);

absl::Status SaveProject(const ProjectState& state, const std::string& path);
absl::Status LoadProject(ProjectState& state, const std::string& path);

std::vector<uint8_t> CaptureProjectState(const ProjectState& state);
absl::Status ApplyProjectState(ProjectState& state,
                               const std::vector<uint8_t>& data);
void SyncProjectToGui(const ProjectState& state);
double GetProjectDuration(const ProjectState& state);
void BounceProject(ProjectState& live_state, const std::string& path);
// Bounce a single track's clip range + tail to a WAV file (no mutex held).
// timeline_clips: deep-copied from live state so MIDI events are preserved.
// snapshot: used only to restore plugins for the target track.
bool BounceTrackClip(
    const std::vector<uint8_t>& snapshot, float sample_rate, double bpm,
    int track_idx,
    const std::vector<std::unique_ptr<TimelineClip>>& timeline_clips,
    double start_sec, double end_sec, const std::string& path);
void sendAutomationLanesData(
    int track_idx, const std::vector<AutomationLane>& lanes,
    const std::vector<std::unique_ptr<IPlugin>>& plugins);

}  // namespace hibiki
