#include <alsa/asoundlib.h>

#include <iostream>

#include "engine/audio/sound.hpp"

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
  SoundDeviceAlsa(int rate, int ch, int latency_ms)
      : sample_rate(rate), channels(ch) {
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
                           1,                   // allow resampling
                           latency_ms * 1000);  // convert ms to us
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

class SoundDeviceAlsaInput : public SoundDevice {
  struct Impl {
    snd_pcm_t* pcm_handle = nullptr;
  };

  std::unique_ptr<Impl> impl;
  int sample_rate;
  int channels;

 public:
  SoundDeviceAlsaInput(const std::string& device_id, int rate, int ch,
                       int latency_ms)
      : sample_rate(rate), channels(ch) {
    impl = std::make_unique<Impl>();
    std::string dev = device_id.empty() ? "default" : device_id;
    if (snd_pcm_open(&impl->pcm_handle, dev.c_str(), SND_PCM_STREAM_CAPTURE,
                     0) < 0) {
      std::cerr << "Cannot open ALSA capture device: " << dev << std::endl;
      return;
    }
    snd_pcm_info_t* info;
    snd_pcm_info_alloca(&info);
    if (snd_pcm_info(impl->pcm_handle, info) == 0) {
      std::cerr << "ALSA Capture Device: " << snd_pcm_info_get_id(info) << " ("
                << snd_pcm_info_get_name(info) << ")\n"
                << std::flush;
    }

    int err =
        snd_pcm_set_params(impl->pcm_handle, SND_PCM_FORMAT_FLOAT_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED, channels, sample_rate,
                           1,                   // allow resampling
                           latency_ms * 1000);  // convert ms to us
    if (err < 0) {
      std::cerr << "ALSA capture parameter setting failed: "
                << snd_strerror(err) << std::endl;
      snd_pcm_close(impl->pcm_handle);
      impl->pcm_handle = nullptr;
    }
  }

  ~SoundDeviceAlsaInput() override {
    if (impl->pcm_handle) {
      snd_pcm_drop(impl->pcm_handle);
      snd_pcm_close(impl->pcm_handle);
      impl->pcm_handle = nullptr;
    }
  }

  int get_sample_rate() const override { return sample_rate; }
  int get_channels() const override { return channels; }
  bool is_ready() const override { return impl->pcm_handle != nullptr; }

  void write(const std::vector<float>&, int) override {}

  bool read(std::vector<float>& interleaved_data, int num_frames) override {
    if (!impl->pcm_handle) return false;
    interleaved_data.resize(num_frames * channels);
    snd_pcm_sframes_t frames =
        snd_pcm_readi(impl->pcm_handle, interleaved_data.data(), num_frames);
    if (frames < 0) {
      frames = snd_pcm_recover(impl->pcm_handle, frames, 0);
      if (frames < 0) {
        std::cerr << "ALSA read failed: " << snd_strerror(frames) << std::endl;
        return false;
      }
      // Retry after recovery
      frames =
          snd_pcm_readi(impl->pcm_handle, interleaved_data.data(), num_frames);
      if (frames < 0) return false;
    }
    return true;
  }
};

}  // namespace

std::unique_ptr<SoundDevice> SoundDevice::create(int rate, int ch,
                                                 int latency_ms) {
  return std::make_unique<SoundDeviceAlsa>(rate, ch, latency_ms);
}

std::unique_ptr<SoundDevice> SoundDevice::createInput(
    const std::string& device_id, int rate, int ch, int latency_ms) {
  return std::make_unique<SoundDeviceAlsaInput>(device_id, rate, ch,
                                                latency_ms);
}

std::vector<AudioInputInfo> SoundDevice::listInputDevices() {
  std::vector<AudioInputInfo> result;
  void** hints = nullptr;
  if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
    std::cerr << "[Input] snd_device_name_hint failed" << std::endl;
    return result;
  }

  for (void** h = hints; *h; ++h) {
    char* name = snd_device_name_get_hint(*h, "NAME");
    char* desc = snd_device_name_get_hint(*h, "DESC");
    char* ioid = snd_device_name_get_hint(*h, "IOID");

    // Skip devices explicitly marked as Output-only
    bool is_output_only = (ioid != nullptr && std::string(ioid) == "Output");
    if (is_output_only || !name) {
      if (name) free(name);
      if (desc) free(desc);
      if (ioid) free(ioid);
      continue;
    }

    // Skip dmix/dsnoop virtual devices that clutter the list
    std::string sname(name);
    if (sname.find("dmix") == 0) {
      free(name);
      if (desc) free(desc);
      if (ioid) free(ioid);
      continue;
    }

    // Try to actually open for capture — skip devices that fail
    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, name, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK) <
        0) {
      free(name);
      if (desc) free(desc);
      if (ioid) free(ioid);
      continue;
    }

    AudioInputInfo info;
    info.id = name;
    info.name = desc ? std::string(desc) : std::string(name);
    // Replace newlines in description
    for (auto& c : info.name) {
      if (c == '\n') c = ' ';
    }
    // Detect channel count
    info.channel_count = 2;  // Default assumption
    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    if (snd_pcm_hw_params_any(pcm, hw) >= 0) {
      unsigned int max_ch = 0;
      snd_pcm_hw_params_get_channels_max(hw, &max_ch);
      if (max_ch > 0 && max_ch <= 32) info.channel_count = (int)max_ch;
    }
    snd_pcm_close(pcm);

    std::cerr << "[Input] Found: " << info.id << " (" << info.name << ", "
              << info.channel_count << " ch)" << std::endl;
    result.push_back(std::move(info));

    if (name) free(name);
    if (desc) free(desc);
    if (ioid) free(ioid);
  }
  snd_device_name_free_hint(hints);
  std::cerr << "[Input] Total devices found: " << result.size() << std::endl;
  return result;
}

}  // namespace hibiki
