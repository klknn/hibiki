#pragma once

#include <vector>

class BasePlayback {
public:
    virtual ~BasePlayback() = default;
    virtual bool is_ready() const = 0;
    virtual void write(const std::vector<float>& interleaved_data, int num_frames) = 0;
};
