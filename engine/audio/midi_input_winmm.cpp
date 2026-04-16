// Windows Multimedia (WinMM) MIDI input implementation.

#include "engine/audio/midi_input.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <mmsystem.h>
#include <windows.h>

#include <cstdio>
#include <mutex>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace hibiki {

class MidiInputWinMM : public MidiInput {
 public:
  ~MidiInputWinMM() override { close(); }

  bool open(const std::string& device_id) override {
    if (handle_) close();

    device_id_ = device_id;

    if (device_id == MIDI_GLOBAL_ID) {
      // Open all available MIDI input devices
      UINT n = midiInGetNumDevs();
      for (UINT i = 0; i < n; i++) {
        HMIDIIN h = nullptr;
        MMRESULT res = midiInOpen(&h, i, (DWORD_PTR)midiCallback,
                                  (DWORD_PTR)this, CALLBACK_FUNCTION);
        if (res == MMSYSERR_NOERROR && h) {
          midiInStart(h);
          handles_.push_back(h);
        }
      }
      if (handles_.empty()) {
        fprintf(stderr, "[MIDI Input] No MIDI devices available\n");
        return false;
      }
      // Set handle_ to first for close() compatibility
      handle_ = handles_[0];
    } else {
      // Open specific device by index
      UINT dev_id = 0;
      try {
        dev_id = (UINT)std::stoul(device_id);
      } catch (...) {
        fprintf(stderr, "[MIDI Input] Invalid device ID: %s\n",
                device_id.c_str());
        return false;
      }

      MMRESULT res = midiInOpen(&handle_, dev_id, (DWORD_PTR)midiCallback,
                                (DWORD_PTR)this, CALLBACK_FUNCTION);
      if (res != MMSYSERR_NOERROR) {
        fprintf(stderr, "[MIDI Input] Failed to open device %u: error %u\n",
                dev_id, res);
        handle_ = nullptr;
        return false;
      }
      midiInStart(handle_);
      handles_.push_back(handle_);
    }

    fprintf(stderr, "[MIDI Input] Opened: %s\n", device_id.c_str());
    return true;
  }

  std::vector<MidiNoteEvent> read() override {
    std::vector<MidiNoteEvent> events;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      events.swap(queue_);
    }
    return events;
  }

  void close() override {
    for (HMIDIIN h : handles_) {
      if (h) {
        midiInStop(h);
        midiInReset(h);
        midiInClose(h);
      }
    }
    handles_.clear();
    handle_ = nullptr;
  }

 private:
  static void CALLBACK midiCallback(HMIDIIN /*hMidiIn*/, UINT wMsg,
                                    DWORD_PTR dwInstance, DWORD_PTR dwParam1,
                                    DWORD_PTR /*dwParam2*/) {
    if (wMsg != MIM_DATA) return;

    auto* self = reinterpret_cast<MidiInputWinMM*>(dwInstance);

    // dwParam1 layout: [0:7]=status [8:15]=data1 [16:23]=data2
    uint8_t status = (uint8_t)(dwParam1 & 0xFF);
    uint8_t data1 = (uint8_t)((dwParam1 >> 8) & 0xFF);
    uint8_t data2 = (uint8_t)((dwParam1 >> 16) & 0xFF);

    uint8_t cmd = status & 0xF0;
    uint8_t ch = status & 0x0F;

    MidiNoteEvent ev;
    ev.sampleOffset = 0;
    ev.channel = ch;
    ev.pitch = data1;

    if (cmd == 0x90) {
      // Note On
      if (data2 > 0) {
        ev.isNoteOn = true;
        ev.velocity = data2 / 127.0f;
      } else {
        // velocity 0 note-on = note-off
        ev.isNoteOn = false;
        ev.velocity = 0.0f;
      }
      std::lock_guard<std::mutex> lock(self->mutex_);
      self->queue_.push_back(ev);
    } else if (cmd == 0x80) {
      // Note Off
      ev.isNoteOn = false;
      ev.velocity = 0.0f;
      std::lock_guard<std::mutex> lock(self->mutex_);
      self->queue_.push_back(ev);
    }
    // Ignore other messages (CC, pitch bend, etc.)
  }

  HMIDIIN handle_ = nullptr;
  std::vector<HMIDIIN> handles_;  // For global mode (all devices)
  std::string device_id_;
  std::mutex mutex_;
  std::vector<MidiNoteEvent> queue_;
};

// Factory
std::unique_ptr<MidiInput> MidiInput::create() {
  return std::make_unique<MidiInputWinMM>();
}

// Enumerate MIDI input devices
std::vector<MidiInputInfo> MidiInput::listDevices() {
  std::vector<MidiInputInfo> result;

  // First entry: Global (All Inputs)
  result.push_back({MIDI_GLOBAL_ID, "Global (All Inputs)", 0});

  UINT n = midiInGetNumDevs();
  for (UINT i = 0; i < n; i++) {
    MIDIINCAPS caps;
    if (midiInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
      std::string id = std::to_string(i);
      std::string name(caps.szPname);
      result.push_back({id, name, 1});
      fprintf(stderr, "[MIDI Input] Found: %s (%s)\n", id.c_str(),
              name.c_str());
    }
  }

  fprintf(stderr, "[MIDI Input] Total devices found: %zu\n", result.size());
  return result;
}

}  // namespace hibiki
