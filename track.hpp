#pragma once

#include <cmath>
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

// Automation control point (sorted by time_beats)
struct AutomationPoint {
    float time_beats = 0.0f;
    float value = 0.0f;       // 0.0 – 1.0
    float tension = 0.0f;     // -1..1 (0=linear, >0=ease-in, <0=ease-out)
};

// Automation lane targeting a single plugin parameter
struct AutomationLane {
    int plugin_idx = 0;
    uint32_t param_id = 0;
    std::vector<AutomationPoint> points;  // sorted by time_beats
};

// Tension-based interpolation between two automation points
// tension=0 → linear, >0 → ease-in (slow start), <0 → ease-out (fast start)
inline float InterpolateAutomation(float v0, float v1, float t, float tension) {
    if (t <= 0.0f) return v0;
    if (t >= 1.0f) return v1;
    // Apply tension curve: t' = t^(2^tension)
    // tension=0 → t^1 (linear), tension=1 → t^2 (ease-in), tension=-1 → t^0.5 (ease-out)
    float exponent = std::pow(2.0f, tension);
    float curved_t = std::pow(t, exponent);
    return v0 + (v1 - v0) * curved_t;
}

// Get the automation value at a given time in beats (binary search + interpolation)
inline float GetAutomationValue(const AutomationLane& lane, double time_beats) {
    if (lane.points.empty()) return 0.0f;
    if (lane.points.size() == 1) return lane.points[0].value;
    if (time_beats <= lane.points.front().time_beats) return lane.points.front().value;
    if (time_beats >= lane.points.back().time_beats) return lane.points.back().value;

    // Binary search for the segment containing time_beats
    size_t lo = 0, hi = lane.points.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (lane.points[mid].time_beats <= time_beats) lo = mid;
        else hi = mid;
    }
    const auto& p0 = lane.points[lo];
    const auto& p1 = lane.points[hi];
    float segment_t = (float)((time_beats - p0.time_beats) / (p1.time_beats - p0.time_beats));
    return InterpolateAutomation(p0.value, p1.value, segment_t, p0.tension);
}

class Track {
public:
    DummyMutex mutex;
    int index;
    std::string name;  // User-defined track name
    std::vector<std::unique_ptr<Vst3Plugin>> plugins;
    std::map<int, std::unique_ptr<Clip>> clips;
    std::vector<std::unique_ptr<TimelineClip>> timeline_clips;
    std::vector<AutomationLane> automation_lanes;

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

    int AddAutomationLane(int plugin_idx, uint32_t param_id);
    bool RemoveAutomationLane(int lane_index);
};

} // namespace hibiki
