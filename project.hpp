#pragma once

#include "track.hpp"
#include <map>
#include <string>
#include <vector>

namespace hibiki {

struct ProjectState {
    std::map<int, std::unique_ptr<Track>> tracks;
    double bpm = 140.0;
    bool is_playing = false;
    double sample_rate = 44100.0;
    
    double playhead_pos_sec = 0.0;
    bool is_timeline_playing = false;
    std::vector<float> levels = {0.0f, 0.0f};

    std::map<int, std::pair<float, float>> track_levels;
    std::mutex tracks_mutex;
    std::mutex levels_mutex;
    bool quit = false;
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

} // namespace hibiki
