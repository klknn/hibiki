#pragma once

#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include "clip.hpp"
#include "vst3_host.hpp"

namespace hibiki {

struct DummyMutex {
    void lock() {}
    void unlock() {}
};

struct TimelineClip {
    std::unique_ptr<Clip> clip;
    double start_time_sec = 0.0;
    double duration_sec = 0.0;    // Duration in seconds (for audio clips)
    double duration_beats = 0.0;  // Duration in beats (for MIDI clips)
};

class Track {
public:
    DummyMutex mutex;
    int index;
    std::string name;  // User-defined track name
    std::vector<std::unique_ptr<Vst3Plugin>> plugins;
    std::map<int, std::unique_ptr<Clip>> clips;
    std::vector<std::unique_ptr<TimelineClip>> timeline_clips;

    int playing_slot = -1;
    double current_time_sec = 0.0;
    int current_midi_idx = 0;

    Track(int idx) : index(idx) {}

    int LoadPlugin(const std::string& path, int plugin_index, double sample_rate);
    bool DeleteClip(int slot);
    bool LoadClip(int slot, const std::string& path, bool is_loop = false);
    void SetClipLoop(int slot, bool is_loop);
    void PlayClip(int slot);
    void Stop();
    bool RemovePlugin(size_t pidx);

    void AddTimelineClip(const std::string& path, double start_time_sec, double bpm, double duration_beats = 0);
    void RemoveTimelineClip(int clip_index);
};

} // namespace hibiki
