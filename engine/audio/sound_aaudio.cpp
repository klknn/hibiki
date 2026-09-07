#include <aaudio/AAudio.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "engine/audio/sound.hpp"

namespace hibiki {

namespace {

/**
 * Android AAudio output device implementation.
 * Provides high-performance, low-latency audio rendering using the Android NDK
 * AAudio API.
 */
class SoundDeviceAAudio : public SoundDevice {
  struct Impl {
    AAudioStream* stream = nullptr;
  };

  std::unique_ptr<Impl> impl;
  int sample_rate;
  int channels;

 public:
  SoundDeviceAAudio(int rate, int ch, int latency_ms)
      : sample_rate(rate), channels(ch) {
    impl = std::make_unique<Impl>();

    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK || !builder) {
      LOG(ERROR) << "AAudio_createStreamBuilder failed: "
                 << AAudio_convertResultToText(result);
      return;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, rate);
    AAudioStreamBuilder_setChannelCount(builder, ch);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setPerformanceMode(builder,
                                           AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);

    result = AAudioStreamBuilder_openStream(builder, &impl->stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK || !impl->stream) {
      LOG(ERROR) << "AAudioStreamBuilder_openStream failed: "
                 << AAudio_convertResultToText(result);
      impl->stream = nullptr;
      return;
    }

    sample_rate = AAudioStream_getSampleRate(impl->stream);
    channels = AAudioStream_getChannelCount(impl->stream);

    result = AAudioStream_requestStart(impl->stream);
    if (result != AAUDIO_OK) {
      LOG(ERROR) << "AAudioStream_requestStart failed: "
                 << AAudio_convertResultToText(result);
      AAudioStream_close(impl->stream);
      impl->stream = nullptr;
      return;
    }

    LOG(INFO) << "AAudio output stream initialized at " << sample_rate
              << " Hz, " << channels << " channels";
  }

  ~SoundDeviceAAudio() override {
    if (impl->stream) {
      AAudioStream_requestStop(impl->stream);
      AAudioStream_close(impl->stream);
      impl->stream = nullptr;
    }
  }

  int get_sample_rate() const override { return sample_rate; }
  int get_channels() const override { return channels; }
  bool is_ready() const override { return impl->stream != nullptr; }

  void write(const std::vector<float>& interleaved_data,
             int num_frames) override {
    if (!impl->stream) return;
    int64_t timeout_nanoseconds = 100000000;  // 100ms timeout
    aaudio_result_t frames_written = AAudioStream_write(
        impl->stream, interleaved_data.data(), num_frames, timeout_nanoseconds);
    if (frames_written < 0) {
      LOG(ERROR) << "AAudioStream_write error: "
                 << AAudio_convertResultToText(frames_written);
    }
  }
};

/**
 * Android AAudio audio capture (input) device implementation.
 */
class SoundDeviceAAudioInput : public SoundDevice {
  struct Impl {
    AAudioStream* stream = nullptr;
  };

  std::unique_ptr<Impl> impl;
  int sample_rate;
  int channels;

 public:
  SoundDeviceAAudioInput(const std::string& device_id, int rate, int ch,
                         int latency_ms)
      : sample_rate(rate), channels(ch) {
    impl = std::make_unique<Impl>();

    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK || !builder) {
      LOG(ERROR) << "AAudio_createStreamBuilder for input failed: "
                 << AAudio_convertResultToText(result);
      return;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, rate);
    AAudioStreamBuilder_setChannelCount(builder, ch);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setPerformanceMode(builder,
                                           AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    result = AAudioStreamBuilder_openStream(builder, &impl->stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK || !impl->stream) {
      LOG(ERROR) << "AAudioStreamBuilder_openStream (input) failed: "
                 << AAudio_convertResultToText(result);
      impl->stream = nullptr;
      return;
    }

    sample_rate = AAudioStream_getSampleRate(impl->stream);
    channels = AAudioStream_getChannelCount(impl->stream);

    result = AAudioStream_requestStart(impl->stream);
    if (result != AAUDIO_OK) {
      LOG(ERROR) << "AAudioStream_requestStart (input) failed: "
                 << AAudio_convertResultToText(result);
      AAudioStream_close(impl->stream);
      impl->stream = nullptr;
      return;
    }

    LOG(INFO) << "AAudio input stream initialized at " << sample_rate << " Hz, "
              << channels << " channels";
  }

  ~SoundDeviceAAudioInput() override {
    if (impl->stream) {
      AAudioStream_requestStop(impl->stream);
      AAudioStream_close(impl->stream);
      impl->stream = nullptr;
    }
  }

  int get_sample_rate() const override { return sample_rate; }
  int get_channels() const override { return channels; }
  bool is_ready() const override { return impl->stream != nullptr; }

  void write(const std::vector<float>&, int) override {}

  bool read(std::vector<float>& interleaved_data, int num_frames) override {
    if (!impl->stream) return false;
    interleaved_data.resize(num_frames * channels);
    int64_t timeout_nanoseconds = 50000000;  // 50ms timeout
    aaudio_result_t frames_read = AAudioStream_read(
        impl->stream, interleaved_data.data(), num_frames, timeout_nanoseconds);
    if (frames_read < 0) {
      LOG(ERROR) << "AAudioStream_read error: "
                 << AAudio_convertResultToText(frames_read);
      return false;
    }
    return frames_read == num_frames;
  }
};

}  // namespace

std::unique_ptr<SoundDevice> SoundDevice::create(int rate, int ch,
                                                 int latency_ms) {
  return std::make_unique<SoundDeviceAAudio>(rate, ch, latency_ms);
}

std::unique_ptr<SoundDevice> SoundDevice::createInput(
    const std::string& device_id, int rate, int ch, int latency_ms) {
  return std::make_unique<SoundDeviceAAudioInput>(device_id, rate, ch,
                                                  latency_ms);
}

std::vector<AudioInputInfo> SoundDevice::listInputDevices() {
  return {{"default", "Default Android Audio Input", 2}};
}

}  // namespace hibiki
