#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include "track.hpp"

struct GlobalState {
    std::atomic<bool> quit{false};
    double bpm = 140.0;
    std::atomic<double> sample_rate{44100.0};
    std::map<int, std::unique_ptr<Track>> tracks;
    std::mutex tracks_mutex;
    std::map<int, std::pair<float, float>> track_levels; // Peak L/R
    std::mutex levels_mutex;

    Track* get_or_create_track(int idx) {
        std::lock_guard<std::mutex> lock(tracks_mutex);
        if (!tracks.count(idx)) {
            tracks[idx] = std::make_unique<Track>(idx);
        }
        return tracks[idx].get();
    }
};
