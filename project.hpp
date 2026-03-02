#pragma once

#include "track.hpp"
#include <map>
#include <string>
#include <vector>

namespace hibiki {

struct ProjectState {
    std::map<int, std::unique_ptr<Track>> tracks;
    double bpm = 120.0;
    bool is_playing = false;
    double sample_rate = 44100.0;
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

} // namespace hibiki
