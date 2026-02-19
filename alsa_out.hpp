#pragma once

#include <vector>


typedef struct _snd_pcm snd_pcm_t;

class AlsaPlayback {
    snd_pcm_t *pcm_handle = nullptr;
    int sample_rate;
    int channels;
public:
    AlsaPlayback(int rate = 44100, int ch = 2);
    ~AlsaPlayback();

    bool is_ready() const;
    void write(const std::vector<float>& interleaved_data, int num_frames);
};
