// Android MIDI input implementation.
// Provides device enumeration and MIDI note event capturing on Android.

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "engine/audio/midi_input.hpp"

namespace hibiki {

namespace {

class MidiInputAndroid : public MidiInput {
 public:
  ~MidiInputAndroid() override { close(); }

  bool open(const std::string& device_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    device_id_ = device_id;
    opened_ = true;
    LOG(INFO) << "Android MIDI Input opened for device: " << device_id;
    return true;
  }

  void close() override {
    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = false;
    device_id_.clear();
    events_.clear();
  }

  bool is_open() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return opened_;
  }

  std::string get_device_id() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return device_id_;
  }

  std::vector<MidiNoteEvent> read() override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MidiNoteEvent> out;
    out.swap(events_);
    return out;
  }

  // Push MIDI event from Android JNI or native callback
  void pushEvent(const MidiNoteEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (opened_) {
      events_.push_back(event);
    }
  }

 private:
  mutable std::mutex mutex_;
  std::string device_id_;
  bool opened_ = false;
  std::vector<MidiNoteEvent> events_;
};

}  // namespace

std::unique_ptr<MidiInput> MidiInput::create() {
  return std::make_unique<MidiInputAndroid>();
}

std::vector<MidiDeviceInfo> MidiInput::listDevices() {
  std::vector<MidiDeviceInfo> devices;
  devices.push_back({MIDI_GLOBAL_ID, "All Android MIDI Inputs"});
  devices.push_back({"virtual", "Virtual Touch Keyboard / Pads"});
  return devices;
}

}  // namespace hibiki
