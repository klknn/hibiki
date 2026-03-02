#pragma once

#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include "clip.hpp"
#include "vst3_host.hpp"

namespace hibiki {

class Track {
public:
    int index;
    std::vector<std::unique_ptr<Vst3Plugin>> plugins;
    std::map<int, std::unique_ptr<Clip>> clips;

    int playing_slot = -1;
    double current_time_sec = 0.0;
    int current_midi_idx = 0;

    std::mutex mutex;

    Track(int idx) : index(idx) {}

    int LoadPlugin(const std::string& path, int plugin_index, double sample_rate);
    bool DeleteClip(int slot);
    bool LoadClip(int slot, const std::string& path, bool is_loop = false);
    void SetClipLoop(int slot, bool is_loop);
    void PlayClip(int slot);
    void Stop();
    bool RemovePlugin(size_t pidx);
};

} // namespace hibiki
