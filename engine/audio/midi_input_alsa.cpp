// ALSA sequencer-based MIDI input implementation.
// Uses snd_seq for device enumeration and event reading.

#include <alsa/asoundlib.h>

#include <cstring>

#include "absl/log/log.h"
#include "engine/audio/midi_input.hpp"

namespace hibiki {

class MidiInputAlsa : public MidiInput {
 public:
  ~MidiInputAlsa() override { close(); }

  bool open(const std::string& device_id) override {
    if (seq_) close();

    int err =
        snd_seq_open(&seq_, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK);
    if (err < 0) {
      LOG(ERROR) << "Failed to open ALSA sequencer: " << snd_strerror(err);
      return false;
    }
    snd_seq_set_client_name(seq_, "hibiki-midi-in");

    // Create an input port
    port_id_ = snd_seq_create_simple_port(
        seq_, "input", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port_id_ < 0) {
      LOG(ERROR) << "Failed to create port: " << snd_strerror(port_id_);
      close();
      return false;
    }

    device_id_ = device_id;

    if (device_id == MIDI_GLOBAL_ID) {
      // Subscribe to all available MIDI output ports
      subscribeAll();
    } else {
      // Parse "client:port" format
      int client = 0, port = 0;
      if (sscanf(device_id.c_str(), "%d:%d", &client, &port) == 2) {
        subscribePort(client, port);
      } else {
        LOG(ERROR) << "Invalid MIDI device ID: " << device_id;
        close();
        return false;
      }
    }

    LOG(INFO) << "MIDI Input opened: " << device_id;
    return true;
  }

  std::vector<MidiNoteEvent> read() override {
    std::vector<MidiNoteEvent> events;
    if (!seq_) return events;

    snd_seq_event_t* ev = nullptr;
    while (snd_seq_event_input(seq_, &ev) >= 0 && ev) {
      MidiNoteEvent mne;
      mne.sampleOffset = 0;  // Real-time: process at start of block

      switch (ev->type) {
        case SND_SEQ_EVENT_NOTEON:
          mne.channel = ev->data.note.channel;
          mne.pitch = ev->data.note.note;
          if (ev->data.note.velocity > 0) {
            mne.isNoteOn = true;
            mne.velocity = ev->data.note.velocity / 127.0f;
          } else {
            // velocity 0 note-on = note-off
            mne.isNoteOn = false;
            mne.velocity = 0.0f;
          }
          events.push_back(mne);
          break;

        case SND_SEQ_EVENT_NOTEOFF:
          mne.channel = ev->data.note.channel;
          mne.pitch = ev->data.note.note;
          mne.isNoteOn = false;
          mne.velocity = 0.0f;
          events.push_back(mne);
          break;

        default:
          // Ignore CC, pitch bend, etc. for now
          break;
      }
    }
    return events;
  }

  void close() override {
    if (seq_) {
      snd_seq_close(seq_);
      seq_ = nullptr;
      port_id_ = -1;
    }
  }

 private:
  void subscribePort(int client, int port) {
    snd_seq_addr_t sender = {(unsigned char)client, (unsigned char)port};
    snd_seq_addr_t dest = {(unsigned char)snd_seq_client_id(seq_),
                           (unsigned char)port_id_};
    snd_seq_port_subscribe_t* sub;
    snd_seq_port_subscribe_alloca(&sub);
    snd_seq_port_subscribe_set_sender(sub, &sender);
    snd_seq_port_subscribe_set_dest(sub, &dest);
    int err = snd_seq_subscribe_port(seq_, sub);
    if (err < 0) {
      LOG(ERROR) << "Failed to subscribe " << client << ":" << port << ": "
                 << snd_strerror(err);
    }
  }

  void subscribeAll() {
    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq_, cinfo) >= 0) {
      int client = snd_seq_client_info_get_client(cinfo);
      if (client == snd_seq_client_id(seq_)) continue;  // Skip self

      snd_seq_port_info_set_client(pinfo, client);
      snd_seq_port_info_set_port(pinfo, -1);
      while (snd_seq_query_next_port(seq_, pinfo) >= 0) {
        unsigned int caps = snd_seq_port_info_get_capability(pinfo);
        unsigned int type = snd_seq_port_info_get_type(pinfo);
        // Only subscribe to readable MIDI ports (hardware or software)
        if ((caps & SND_SEQ_PORT_CAP_READ) &&
            (caps & SND_SEQ_PORT_CAP_SUBS_READ) &&
            (type &
             (SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_HARDWARE))) {
          int port = snd_seq_port_info_get_port(pinfo);
          subscribePort(client, port);
          LOG(INFO) << "MIDI Input subscribed to " << client << ":" << port
                    << " (" << snd_seq_port_info_get_name(pinfo) << ")";
        }
      }
    }
  }

  snd_seq_t* seq_ = nullptr;
  int port_id_ = -1;
  std::string device_id_;
};

// Factory
std::unique_ptr<MidiInput> MidiInput::create() {
  return std::make_unique<MidiInputAlsa>();
}

// Enumerate MIDI input devices
std::vector<MidiInputInfo> MidiInput::listDevices() {
  std::vector<MidiInputInfo> result;

  // First entry: Global (All Inputs)
  result.push_back({MIDI_GLOBAL_ID, "Global (All Inputs)", 0});

  snd_seq_t* seq = nullptr;
  int err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK);
  if (err < 0) {
    LOG(ERROR) << "Failed to open seq for enumeration: " << snd_strerror(err);
    return result;
  }

  snd_seq_client_info_t* cinfo;
  snd_seq_port_info_t* pinfo;
  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);

  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(seq, cinfo) >= 0) {
    int client = snd_seq_client_info_get_client(cinfo);
    const char* client_name = snd_seq_client_info_get_name(cinfo);

    // Skip the System client (client 0) and our own client
    if (client == 0 || client == snd_seq_client_id(seq)) continue;

    snd_seq_port_info_set_client(pinfo, client);
    snd_seq_port_info_set_port(pinfo, -1);
    int port_count = 0;

    while (snd_seq_query_next_port(seq, pinfo) >= 0) {
      unsigned int caps = snd_seq_port_info_get_capability(pinfo);
      if ((caps & SND_SEQ_PORT_CAP_READ) &&
          (caps & SND_SEQ_PORT_CAP_SUBS_READ)) {
        int port = snd_seq_port_info_get_port(pinfo);
        const char* port_name = snd_seq_port_info_get_name(pinfo);

        std::string id = std::to_string(client) + ":" + std::to_string(port);
        std::string name = std::string(client_name);
        if (port_name && strlen(port_name) > 0 &&
            strcmp(client_name, port_name) != 0) {
          name += " - " + std::string(port_name);
        }

        result.push_back({id, name, 1});
        port_count++;
        LOG(INFO) << "MIDI Input found: " << id << " (" << name << ")";
      }
    }
  }

  LOG(INFO) << "MIDI Input total devices found: " << result.size();
  snd_seq_close(seq);
  return result;
}

}  // namespace hibiki
