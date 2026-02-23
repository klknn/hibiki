#pragma once

#include <vector>


#include "playback_base.hpp"

typedef struct _snd_pcm snd_pcm_t;

class AlsaPlayback : public BasePlayback {
    snd_pcm_t *pcm_handle = nullptr;
    int sample_rate;
    int channels;
public:
    AlsaPlayback(int rate = 44100, int ch = 2);
    ~AlsaPlayback() override;

    bool is_ready() const override;
    void write(const std::vector<float>& interleaved_data, int num_frames) override;
};
