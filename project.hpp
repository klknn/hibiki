#pragma once

#include <limits>
#include <map>
#include <string>
#include <vector>

#include "plugin_proxy.hpp"
#include "track.hpp"
#include "vst3_host.hpp"

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
};

// Returns a pointer to the track, creating it if it doesn't exist
Track* GetOrCreateTrack(ProjectState& state, int track_index);

bool SaveProject(const ProjectState& state, const std::string& path);
bool LoadProject(ProjectState& state, const std::string& path);

std::vector<uint8_t> CaptureProjectState(const ProjectState& state);
bool ApplyProjectState(ProjectState& state, const std::vector<uint8_t>& data);
void SyncProjectToGui(const ProjectState& state);
double GetProjectDuration(const ProjectState& state);
void BounceProject(ProjectState& live_state, const std::string& path);
void sendAutomationLanesData(
    int track_idx, const std::vector<AutomationLane>& lanes,
    const std::vector<std::unique_ptr<IPlugin>>& plugins);

}  // namespace hibiki
