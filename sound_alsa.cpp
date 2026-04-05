#include <alsa/asoundlib.h>

#include <iostream>

#include "sound.hpp"

namespace hibiki {

namespace {

class SoundDeviceAlsa : public SoundDevice {
  struct Impl {
    snd_pcm_t* pcm_handle = nullptr;
  };

  std::unique_ptr<Impl> impl;
  int sample_rate;
  int channels;

 public:
  SoundDeviceAlsa(int rate, int ch) : sample_rate(rate), channels(ch) {
    impl = std::make_unique<Impl>();
    if (snd_pcm_open(&impl->pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) <
        0) {
      std::cerr << "Cannot open ALSA audio device" << std::endl;
      return;
    }
    snd_pcm_info_t* info;
    snd_pcm_info_alloca(&info);
    if (snd_pcm_info(impl->pcm_handle, info) == 0) {
      std::cerr << "ALSA Audio Device: " << snd_pcm_info_get_id(info) << " ("
                << snd_pcm_info_get_name(info) << ")\n"
                << std::flush;
    }

    int err =
        snd_pcm_set_params(impl->pcm_handle, SND_PCM_FORMAT_FLOAT_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED, channels, sample_rate,
                           1,       // allow resampling
                           50000);  // 50ms latency
    if (err < 0) {
      std::cerr << "ALSA parameter setting failed: " << snd_strerror(err)
                << std::endl;
    }
  }

  ~SoundDeviceAlsa() override {
    if (impl->pcm_handle) {
      snd_pcm_drain(impl->pcm_handle);
      snd_pcm_close(impl->pcm_handle);
      impl->pcm_handle = nullptr;
    }
  }

  int get_sample_rate() const override { return sample_rate; }
  int get_channels() const override { return channels; }
  bool is_ready() const override { return impl->pcm_handle != nullptr; }

  void write(const std::vector<float>& interleaved_data,
             int num_frames) override {
    if (!impl->pcm_handle) return;
    snd_pcm_sframes_t frames =
        snd_pcm_writei(impl->pcm_handle, interleaved_data.data(), num_frames);
    if (frames < 0) {
      frames = snd_pcm_recover(impl->pcm_handle, frames, 0);
      if (frames < 0) {
        std::cerr << "ALSA write failed: " << snd_strerror(frames) << std::endl;
      }
    }
  }
};

}  // namespace

std::unique_ptr<SoundDevice> SoundDevice::create(int rate, int ch) {
  return std::make_unique<SoundDeviceAlsa>(rate, ch);
}

}  // namespace hibiki
