#pragma once

#include <atomic>
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
  static constexpr int kMaxSteps = 64;
  std::vector<bool> steps = std::vector<bool>(kMaxSteps, false);
  std::vector<float> step_velocities = std::vector<float>(kMaxSteps, 0.78f);
  std::vector<float> step_pitches = std::vector<float>(kMaxSteps, 0.0f);
  std::vector<float> step_pans = std::vector<float>(kMaxSteps, 0.0f);
  std::vector<float> step_shifts = std::vector<float>(kMaxSteps, 0.0f);
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
  std::atomic<double> bpm{
      std::numeric_limits<double>::quiet_NaN()};  // NaN to catch uninitialized
                                                  // BPM bugs
  std::atomic<bool> is_playing{false};
  std::atomic<double> sample_rate{
      std::numeric_limits<double>::quiet_NaN()};  // NaN to catch uninitialized
                                                  // sample_rate bugs

  std::atomic<double> playhead_pos_sec{0.0};
  std::atomic<bool> is_timeline_playing{false};
  std::vector<float> levels = {0.0f, 0.0f};

  std::map<int, std::pair<float, float>> track_levels;
  std::mutex tracks_mutex;
  std::mutex levels_mutex;
  std::atomic<bool> quit{false};

  // Plugin hosting mode (set via SetPluginHostMode command)
  PluginHostMode plugin_host_mode = PluginHostMode::IN_PROCESS;
  std::vector<std::string> remote_hosts;  // ["host:port", ...] for REMOTE mode
  std::atomic<int> buffer_latency_ms{200};        // Audio buffer latency in ms
  std::atomic<bool> use_double_precision{false};  // 64-bit float engine path
  std::atomic<bool> audio_restart_requested{false};

  // Recording state
  std::atomic<bool> is_recording{false};
  std::atomic<double> record_start_sec{0.0};  // Playhead position at rec start
  std::string project_dir;                    // "" = unsaved, set on save/load

  // Loop region
  std::atomic<bool> loop_enabled{false};
  std::atomic<double> loop_start_sec{0.0};
  std::atomic<double> loop_end_sec{0.0};

  // Global transport/mixer/telemetry state
  std::atomic<bool> metronome_enabled{false};
  std::atomic<float> master_volume{0.82f};
  std::atomic<float> master_pitch{0.0f};
  int cpu_percent = 0;
  std::vector<float> oscilloscope;

  // Channel Rack & Pattern Sequencer state
  std::atomic<SequencerPlaybackMode> playback_mode{SequencerPlaybackMode::PAT};
  std::atomic<float> swing_amount{0.0f};
  std::atomic<int> selected_pattern_index{0};
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
struct RenderProjectWorkspace {
  static constexpr int kMaxBlockSize = 4096;
  static constexpr int kMaxBusTracks = 512;
  std::vector<double> bufferL;
  std::vector<double> bufferR;
  std::vector<double> dryBufL;
  std::vector<double> dryBufR;
  std::vector<float> plugBufL;
  std::vector<float> plugBufR;
  std::vector<double> mixBufferL;
  std::vector<double> mixBufferR;
  std::vector<MidiNoteEvent> clipMidiEvents;
  std::vector<MidiNoteEvent> timelineMidiEvents;
  std::vector<MidiNoteEvent> allEvents;
  std::vector<float> input_block;
  int bus_indices[kMaxBusTracks]{};
  int bus_indeg[kMaxBusTracks]{};
  int bus_out[kMaxBusTracks]{};
  int bus_queue[kMaxBusTracks]{};
  bool bus_visited[kMaxBusTracks]{};

  void reserve(int block_size) {
    const int n = std::min(block_size, kMaxBlockSize);
    bufferL.assign(n, 0.0);
    bufferR.assign(n, 0.0);
    dryBufL.assign(n, 0.0);
    dryBufR.assign(n, 0.0);
    plugBufL.assign(n, 0.0f);
    plugBufR.assign(n, 0.0f);
    mixBufferL.assign(n, 0.0);
    mixBufferR.assign(n, 0.0);
    clipMidiEvents.reserve(512);
    timelineMidiEvents.reserve(512);
    allEvents.reserve(512);
    input_block.reserve(n * 8);
  }
};

struct RenderBlockOptions {
  bool capture_live_inputs = false;
  bool update_meters = false;
  bool render_metronome = false;
  double cur_bpm = 120.0;
  double cur_playhead_sec = 0.0;
  float sample_rate = 44100.0f;
  bool cur_playing = false;
  bool cur_recording = false;
};

// Renders one stereo block of the entire project (Pass 1 Normal -> Pass 2
// topological Group/Aux DAG -> Metronome -> Pass 3 Master -> Global Volume)
// into ws.mixBufferL / ws.mixBufferR. Caller must hold state.tracks_mutex in
// live playback (or own an isolated ProjectState during offline bounce).
void RenderProjectAudioBlock(ProjectState& state, HostProcessContext& context,
                             RenderProjectWorkspace& ws, int block_size,
                             const RenderBlockOptions& opts);

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
