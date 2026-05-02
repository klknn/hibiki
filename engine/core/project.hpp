#pragma once

#include <limits>
#include <map>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "engine/core/track.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

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
};

// Convert a duration in beats to seconds at the given BPM.
double beatsToSec(double beats, double bpm);

// Returns a pointer to the track, creating it if it doesn't exist
Track* GetOrCreateTrack(ProjectState& state, int track_index);

absl::Status SaveProject(const ProjectState& state, const std::string& path);
absl::Status LoadProject(ProjectState& state, const std::string& path);

std::vector<uint8_t> CaptureProjectState(const ProjectState& state);
absl::Status ApplyProjectState(ProjectState& state,
                               const std::vector<uint8_t>& data);
void SyncProjectToGui(const ProjectState& state);
double GetProjectDuration(const ProjectState& state);
void BounceProject(ProjectState& live_state, const std::string& path);
void sendAutomationLanesData(
    int track_idx, const std::vector<AutomationLane>& lanes,
    const std::vector<std::unique_ptr<IPlugin>>& plugins);

}  // namespace hibiki
