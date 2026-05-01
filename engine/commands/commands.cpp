#include "engine/commands/commands.hpp"

#include <string>

#include "absl/log/log.h"
#include "engine/audio/midi_input.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/notifications.pb.h"

namespace hibiki {

void handleListAudioInputs() {
  auto devices = SoundDevice::listInputDevices();
  pb::notifications::Notification notif;
  auto* list = notif.mutable_audio_input_list();
  for (const auto& dev : devices) {
    auto* d = list->add_devices();
    d->set_id(dev.id);
    d->set_name(dev.name);
    d->set_channel_count(dev.channel_count);
  }
  std::string data;
  notif.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void handleListMidiInputs() {
  auto devices = MidiInput::listDevices();
  pb::notifications::Notification notif;
  auto* list = notif.mutable_midi_input_list();
  for (const auto& dev : devices) {
    auto* d = list->add_devices();
    d->set_id(dev.id);
    d->set_name(dev.name);
    d->set_port_count(dev.port_count);
  }
  std::string data;
  notif.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

}  // namespace hibiki
