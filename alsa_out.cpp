#include "alsa_out.hpp"
#include <iostream>

AlsaPlayback::AlsaPlayback(int rate, int ch) : sample_rate(rate), channels(ch) {
    if (snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Cannot open ALSA audio device" << std::endl;
        return;
    }
    snd_pcm_info_t* info;
    snd_pcm_info_alloca(&info);
    if (snd_pcm_info(pcm_handle, info) == 0) {
        std::cout << "ALSA Audio Device: " << snd_pcm_info_get_id(info) 
                  << " (" << snd_pcm_info_get_name(info) << ")\n" << std::flush;
    }

    int err = snd_pcm_set_params(pcm_handle, 
                                 SND_PCM_FORMAT_FLOAT_LE, 
                                 SND_PCM_ACCESS_RW_INTERLEAVED, 
                                 channels, 
                                 sample_rate, 
                                 1,     // allow resampling
                                 50000); // 50ms latency
    if (err < 0) {
        std::cerr << "ALSA parameter setting failed: " << snd_strerror(err) << std::endl;
    }
}

AlsaPlayback::~AlsaPlayback() {
    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
        pcm_handle = nullptr;
    }
}

bool AlsaPlayback::is_ready() const { 
    return pcm_handle != nullptr; 
}

void AlsaPlayback::write(const std::vector<float>& interleaved_data, int num_frames) {
    if (!pcm_handle) return;
    snd_pcm_sframes_t frames = snd_pcm_writei(pcm_handle, interleaved_data.data(), num_frames);
    if (frames < 0) {
        frames = snd_pcm_recover(pcm_handle, frames, 0);
        if (frames < 0) {
            std::cerr << "ALSA write failed: " << snd_strerror(frames) << std::endl;
        }
    }
}
