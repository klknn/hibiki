#pragma once

#include <vector>
#include <atomic>
#include <memory>

class CoreAudioPlayback {
public:
    struct Impl;
    CoreAudioPlayback(int rate = 44100, int ch = 2);
    ~CoreAudioPlayback();

    bool is_ready() const;
    void write(const std::vector<float>& interleaved_data, int num_frames);

    std::unique_ptr<Impl> impl;
};
