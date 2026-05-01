#include "engine/commands/commands.hpp"

#include <algorithm>
#include <mutex>
#include <string>

#include "engine/core/modulator.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

static void sendModulationInfo(Track* track, int plugin_idx) {
  pb::notifications::Notification notif;
  auto* info = notif.mutable_modulation_info();
  info->set_track_index(track->index);
  info->set_plugin_index(plugin_idx);

  auto it = track->modulations.find(plugin_idx);
  if (it != track->modulations.end()) {
    for (int s = 0; s < Modulator::kMaxSlots; ++s) {
      auto& mod = it->second.slots[s];
      auto* slot = info->add_slots();
      slot->set_slot_index(s);
      slot->set_waveform(static_cast<int>(mod.waveform));
      slot->set_rate_hz(mod.rate_hz);
      slot->set_depth(mod.depth);
      slot->set_target_param_id(mod.param_id);
      slot->set_target_param_name(mod.param_name);
      slot->set_assigned(mod.assigned);
      slot->set_sync_to_tempo(mod.sync_to_tempo);
    }
  }

  std::string data;
  notif.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void handleModulationCmd(const pb::commands::ModulationCmd& cmd,
                         ProjectState& state) {
  int tidx = cmd.target().track_index();
  int pidx = cmd.target().plugin_index();
  int slot = cmd.slot_index();

  std::lock_guard<std::mutex> lock(state.tracks_mutex);
  if (!state.tracks.count(tidx)) return;
  auto& track = state.tracks[tidx];
  if (pidx < 0 || pidx >= (int)track->plugins.size()) return;
  if (slot < 0 || slot >= Modulator::kMaxSlots) return;

  auto& pmod = track->modulations[pidx];
  auto& mod = pmod.slots[slot];

  switch (cmd.action()) {
    case pb::commands::ModulationCmd::ACTION_ADD: {
      mod.waveform =
          static_cast<Modulator::Waveform>(std::clamp(cmd.waveform(), 0, 3));
      mod.rate_hz = cmd.rate_hz() > 0 ? cmd.rate_hz() : 1.0f;
      mod.depth = std::clamp(cmd.depth(), -1.0f, 1.0f);
      mod.sync_to_tempo = cmd.sync_to_tempo();
      mod.plugin_idx = pidx;
      mod.reset();
      break;
    }
    case pb::commands::ModulationCmd::ACTION_REMOVE: {
      mod = Modulator{};  // Reset to defaults
      break;
    }
    case pb::commands::ModulationCmd::ACTION_CONFIGURE: {
      mod.waveform =
          static_cast<Modulator::Waveform>(std::clamp(cmd.waveform(), 0, 3));
      mod.rate_hz = cmd.rate_hz() > 0 ? cmd.rate_hz() : mod.rate_hz;
      mod.depth = std::clamp(cmd.depth(), -1.0f, 1.0f);
      mod.sync_to_tempo = cmd.sync_to_tempo();
      break;
    }
    case pb::commands::ModulationCmd::ACTION_ASSIGN: {
      mod.plugin_idx = pidx;
      mod.param_id = cmd.target_param_id();
      mod.assigned = true;

      // Look up the param name for display
      VstParamInfo pinfo;
      auto* plugin = track->plugins[pidx].get();
      int param_count = plugin->getParameterCount();
      for (int i = 0; i < param_count; ++i) {
        if (plugin->getParameterInfo(i, pinfo) &&
            pinfo.id == cmd.target_param_id()) {
          mod.param_name = pinfo.name;
          break;
        }
      }
      break;
    }
    default:
      return;
  }

  sendModulationInfo(track.get(), pidx);
}

}  // namespace hibiki
