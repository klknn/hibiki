#pragma once

#include <memory>
#include <vector>

namespace hibiki {

class SoundDevice {
 public:
  virtual ~SoundDevice() = default;

  virtual int get_sample_rate() const = 0;
  virtual int get_channels() const = 0;
  virtual bool is_ready() const = 0;

  // existing abstraction for output
  virtual void write(const std::vector<float>& interleaved_data,
                     int num_frames) = 0;

  static std::unique_ptr<SoundDevice> create(int rate = 44100, int ch = 2);
};

}  // namespace hibiki
