#pragma once

#include <vector>

class Win32Playback {
    struct Impl;
    Impl* impl;
    int sample_rate;
    int channels;
public:
    Win32Playback(int rate = 44100, int ch = 2);
    ~Win32Playback();

    bool is_ready() const;
    void write(const std::vector<float>& interleaved_data, int num_frames);
};
