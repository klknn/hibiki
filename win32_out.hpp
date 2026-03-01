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

<<<<<<< HEAD
    int get_sample_rate() const { return sample_rate; }
    int get_channels() const { return channels; }
=======
>>>>>>> 47601e7bb99debf560fbb194795a6862d325182c
    bool is_ready() const;
    void write(const std::vector<float>& interleaved_data, int num_frames);
};
