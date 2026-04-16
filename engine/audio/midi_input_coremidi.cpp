// macOS CoreMIDI-based MIDI input implementation.

#include <CoreMIDI/CoreMIDI.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "absl/strings/numbers.h"
#include "engine/audio/midi_input.hpp"

namespace hibiki {

class MidiInputCoreMidi : public MidiInput {
 public:
  ~MidiInputCoreMidi() override { close(); }

  bool open(const std::string& device_id) override {
    if (client_) close();

    OSStatus status =
        MIDIClientCreate(CFSTR("hibiki-midi-in"), nullptr, nullptr, &client_);
    if (status != noErr) {
      fprintf(stderr, "[MIDI Input] Failed to create CoreMIDI client: %d\n",
              (int)status);
      return false;
    }

    status =
        MIDIInputPortCreate(client_, CFSTR("input"), readProc, this, &port_);
    if (status != noErr) {
      fprintf(stderr, "[MIDI Input] Failed to create input port: %d\n",
              (int)status);
      close();
      return false;
    }

    device_id_ = device_id;

    if (device_id == MIDI_GLOBAL_ID) {
      // Subscribe to all available MIDI sources
      ItemCount n = MIDIGetNumberOfSources();
      for (ItemCount i = 0; i < n; i++) {
        MIDIEndpointRef src = MIDIGetSource(i);
        MIDIPortConnectSource(port_, src, nullptr);
      }
    } else {
      // Find source by unique ID
      int uid = 0;
      if (!absl::SimpleAtoi(device_id, &uid)) {
        fprintf(stderr, "[MIDI Input] Invalid device ID: %s\n",
                device_id.c_str());
        close();
        return false;
      }
      MIDIEndpointRef src = 0;
      ItemCount n = MIDIGetNumberOfSources();
      for (ItemCount i = 0; i < n; i++) {
        MIDIEndpointRef s = MIDIGetSource(i);
        SInt32 id = 0;
        MIDIObjectGetIntegerProperty(s, kMIDIPropertyUniqueID, &id);
        if (id == uid) {
          src = s;
          break;
        }
      }
      if (!src) {
        fprintf(stderr, "[MIDI Input] Source not found: %s\n",
                device_id.c_str());
        close();
        return false;
      }
      MIDIPortConnectSource(port_, src, nullptr);
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
    if (port_) {
      MIDIPortDispose(port_);
      port_ = 0;
    }
    if (client_) {
      MIDIClientDispose(client_);
      client_ = 0;
    }
  }

 private:
  static void readProc(const MIDIPacketList* pktList, void* readProcRefCon,
                       void* /*srcConnRefCon*/) {
    auto* self = static_cast<MidiInputCoreMidi*>(readProcRefCon);
    const MIDIPacket* pkt = &pktList->packet[0];

    std::lock_guard<std::mutex> lock(self->mutex_);
    for (UInt32 i = 0; i < pktList->numPackets; i++) {
      // Parse raw MIDI bytes
      for (UInt16 j = 0; j < pkt->length;) {
        uint8_t status = pkt->data[j];
        uint8_t cmd = status & 0xF0;
        uint8_t ch = status & 0x0F;

        if (cmd == 0x90 && j + 2 < pkt->length) {
          // Note On
          MidiNoteEvent ev;
          ev.sampleOffset = 0;
          ev.channel = ch;
          ev.pitch = pkt->data[j + 1];
          uint8_t vel = pkt->data[j + 2];
          if (vel > 0) {
            ev.isNoteOn = true;
            ev.velocity = vel / 127.0f;
          } else {
            // velocity 0 note-on = note-off
            ev.isNoteOn = false;
            ev.velocity = 0.0f;
          }
          self->queue_.push_back(ev);
          j += 3;
        } else if (cmd == 0x80 && j + 2 < pkt->length) {
          // Note Off
          MidiNoteEvent ev;
          ev.sampleOffset = 0;
          ev.channel = ch;
          ev.pitch = pkt->data[j + 1];
          ev.isNoteOn = false;
          ev.velocity = 0.0f;
          self->queue_.push_back(ev);
          j += 3;
        } else if (cmd == 0xC0 || cmd == 0xD0) {
          j += 2;  // Program Change / Channel Pressure: 2 bytes
        } else if (cmd == 0xF0) {
          // SysEx or real-time — skip to end
          break;
        } else if (status & 0x80) {
          j += 3;  // Other 3-byte messages (CC, pitch bend, etc.)
        } else {
          j++;  // Running status or unknown
        }
      }
      pkt = MIDIPacketNext(pkt);
    }
  }

  MIDIClientRef client_ = 0;
  MIDIPortRef port_ = 0;
  std::string device_id_;
  std::mutex mutex_;
  std::vector<MidiNoteEvent> queue_;
};

// Factory
std::unique_ptr<MidiInput> MidiInput::create() {
  return std::make_unique<MidiInputCoreMidi>();
}

static std::string getEndpointName(MIDIEndpointRef endpoint) {
  CFStringRef name = nullptr;
  MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name);
  if (!name) {
    MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name);
  }
  if (!name) return "(unknown)";
  char buf[256];
  CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
  CFRelease(name);
  return std::string(buf);
}

// Enumerate MIDI input devices
std::vector<MidiInputInfo> MidiInput::listDevices() {
  std::vector<MidiInputInfo> result;

  // First entry: Global (All Inputs)
  result.push_back({MIDI_GLOBAL_ID, "Global (All Inputs)", 0});

  ItemCount n = MIDIGetNumberOfSources();
  for (ItemCount i = 0; i < n; i++) {
    MIDIEndpointRef src = MIDIGetSource(i);
    SInt32 uid = 0;
    MIDIObjectGetIntegerProperty(src, kMIDIPropertyUniqueID, &uid);

    std::string name = getEndpointName(src);
    std::string id = std::to_string(uid);

    result.push_back({id, name, 1});
    fprintf(stderr, "[MIDI Input] Found: %s (%s)\n", id.c_str(), name.c_str());
  }

  fprintf(stderr, "[MIDI Input] Total devices found: %zu\n", result.size());
  return result;
}

}  // namespace hibiki
