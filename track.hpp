#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include "clip.hpp"
#include "vst3_host.hpp"

class Track {
public:
    int index;
    std::vector<std::unique_ptr<Vst3Plugin>> plugins;
    std::map<int, std::unique_ptr<Clip>> clips;

    int playing_slot = -1;
    double current_time_sec = 0.0;
    int current_midi_idx = 0;

    std::mutex mutex;

    Track(int idx);

    int load_plugin(const std::string& path, int plugin_index, double sample_rate);
    bool delete_clip(int slot);
    bool load_clip(int slot, const std::string& path, bool is_loop = false);
    void set_clip_loop(int slot, bool is_loop);
    void play_clip(int slot);
    void stop();
    bool remove_plugin(size_t pidx);
};
