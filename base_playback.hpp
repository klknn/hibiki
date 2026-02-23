#pragma once

#include <vector>

class Playback {
public:
    virtual ~Playback() = default;
    virtual bool is_ready() const = 0;
    virtual void write(const std::vector<float>& interleaved_data, int num_frames) = 0;
};
