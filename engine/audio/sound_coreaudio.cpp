#include <AudioUnit/AudioUnit.h>

#include <algorithm>
#include <cmath>
#include <thread>

#include "absl/log/log.h"
#include "engine/audio/ring_buffer.hpp"
#include "engine/audio/sound.hpp"

namespace hibiki {

namespace {

class SoundDeviceCoreAudio : public SoundDevice {
  struct Impl {
    AudioUnit audioUnit;
    int sampleRate;
    int channels;
    bool ready = false;
    AudioRingBufferHandle ring;

    Impl(int rate, int ch, int latency_ms)
        : sampleRate(rate),
          channels(ch),
          ring(std::max<size_t>(
              2048,
              ((static_cast<size_t>(rate) * latency_ms) / 1000) * ch * 3)) {}
  };

  std::unique_ptr<Impl> impl;
  int sample_rate;
  int channels;

  static OSStatus PlaybackCallback(void* inRefCon,
                                   AudioUnitRenderActionFlags* ioActionFlags,
                                   const AudioTimeStamp* inTimeStamp,
                                   UInt32 inBusNumber, UInt32 inNumberFrames,
                                   AudioBufferList* ioData) {
    auto* impl = static_cast<Impl*>(inRefCon);
    float* outL = static_cast<float*>(ioData->mBuffers[0].mData);
    float* outR = static_cast<float*>(ioData->mBuffers[1].mData);

    impl->ring.readStereo(outL, outR, inNumberFrames);
    return noErr;
  }

 public:
  SoundDeviceCoreAudio(int rate, int ch, int latency_ms)
      : sample_rate(rate), channels(ch) {
    impl = std::make_unique<Impl>(rate, ch, latency_ms);
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) {
      LOG(ERROR) << "CoreAudio: Failed to find component";
      return;
    }

    if (AudioComponentInstanceNew(comp, &impl->audioUnit) != noErr) {
      LOG(ERROR) << "CoreAudio: Failed to create AudioUnit instance";
      return;
    }

    if (AudioUnitInitialize(impl->audioUnit) != noErr) {
      LOG(ERROR) << "CoreAudio: Failed to initialize AudioUnit";
      return;
    }

    AudioStreamBasicDescription format;
    format.mSampleRate = rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat |
                          kAudioFormatFlagIsNonInterleaved |
                          kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float);
    format.mChannelsPerFrame = 2;  // Fixed to stereo for now
    format.mBitsPerChannel = 32;

    if (AudioUnitSetProperty(impl->audioUnit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &format,
                             sizeof(format)) != noErr) {
      LOG(ERROR) << "CoreAudio: Failed to set stream format";
      return;
    }

    AURenderCallbackStruct callback;
    callback.inputProc = PlaybackCallback;
    callback.inputProcRefCon = impl.get();

    if (AudioUnitSetProperty(
            impl->audioUnit, kAudioUnitProperty_SetRenderCallback,
            kAudioUnitScope_Global, 0, &callback, sizeof(callback)) != noErr) {
      LOG(ERROR) << "CoreAudio: Failed to set render callback";
      return;
    }

    if (AudioOutputUnitStart(impl->audioUnit) != noErr) {
      LOG(ERROR) << "CoreAudio: Failed to start AudioUnit";
      return;
    }

    impl->ready = true;
  }

  ~SoundDeviceCoreAudio() override {
    if (impl->ready) {
      AudioOutputUnitStop(impl->audioUnit);
      AudioUnitUninitialize(impl->audioUnit);
      AudioComponentInstanceDispose(impl->audioUnit);
    }
  }

  int get_sample_rate() const override { return sample_rate; }
  int get_channels() const override { return channels; }
  bool is_ready() const override { return impl->ready; }

  void write(const std::vector<float>& interleaved_data,
             int num_frames) override {
    if (!impl->ready) return;

    size_t count = interleaved_data.size();

    // Simple back-pressure if buffer is full
    while (impl->ring.availableWrite() < count) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    impl->ring.write(interleaved_data.data(), count);
  }
};

}  // namespace

std::unique_ptr<SoundDevice> SoundDevice::create(int rate, int ch,
                                                 int latency_ms) {
  return std::make_unique<SoundDeviceCoreAudio>(rate, ch, latency_ms);
}

std::unique_ptr<SoundDevice> SoundDevice::createInput(
    const std::string& /*device_id*/, int rate, int ch, int latency_ms) {
  // TODO(klknn): Implement CoreAudio input capture with HALOutput AudioUnit.
  // For now return a non-ready device.
  return std::make_unique<SoundDeviceCoreAudio>(rate, ch, latency_ms);
}

std::vector<AudioInputInfo> SoundDevice::listInputDevices() {
  std::vector<AudioInputInfo> result;
  // TODO(klknn): Enumerate CoreAudio input devices via
  // AudioObjectGetPropertyData(kAudioHardwarePropertyDevices).
  AudioInputInfo defaultDev;
  defaultDev.id = "default";
  defaultDev.name = "Default Input";
  defaultDev.channel_count = 2;
  result.push_back(defaultDev);
  return result;
}

}  // namespace hibiki
