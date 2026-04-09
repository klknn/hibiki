#pragma once

#include <memory>
#include <string>
#include <vector>

namespace hibiki {

struct AudioInputInfo {
  std::string id;    // Platform device ID (e.g. "hw:0,0" or CoreAudio UID)
  std::string name;  // Human-readable name
  int channel_count = 2;  // Number of input channels
};

class SoundDevice {
 public:
  virtual ~SoundDevice() = default;

  virtual int get_sample_rate() const = 0;
  virtual int get_channels() const = 0;
  virtual bool is_ready() const = 0;

  // Output: write interleaved audio to the device
  virtual void write(const std::vector<float>& interleaved_data,
                     int num_frames) = 0;

  // Input: read interleaved audio from the device (default: no-op)
  virtual bool read(std::vector<float>& interleaved_data, int num_frames) {
    return false;
  }

  // Create an output device
  static std::unique_ptr<SoundDevice> create(int rate = 44100, int ch = 2,
                                             int latency_ms = 200);

  // Create an input (capture) device
  static std::unique_ptr<SoundDevice> createInput(
      const std::string& device_id = "", int rate = 44100, int ch = 2,
      int latency_ms = 200);

  // List available audio input devices
  static std::vector<AudioInputInfo> listInputDevices();
};

}  // namespace hibiki

