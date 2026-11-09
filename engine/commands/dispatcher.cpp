#include "engine/commands/dispatcher.hpp"

#include <string>

#include "absl/log/log.h"
#include "engine/commands/commands.hpp"
#include "pb/commands.pb.h"

namespace hibiki {

bool dispatchIpcCommand(const uint8_t* data, size_t size, ProjectState& state,
                        HistoryManager& history) {
  hibiki::pb::commands::Request request;
  if (!request.ParseFromArray(data, static_cast<int>(size))) {
    LOG(ERROR) << "BACKEND ERROR: Failed to parse protobuf request";
    return false;
  }

  switch (request.command_case()) {
    case hibiki::pb::commands::Request::kProject:
      handleProjectCmd(request.project(), state, history);
      break;
    case hibiki::pb::commands::Request::kTransport:
      handleTransportCmd(request.transport(), state);
      break;
    case hibiki::pb::commands::Request::kTrack:
      handleTrackCmd(request.track(), state, history);
      break;
    case hibiki::pb::commands::Request::kPlugin:
      handlePluginCmd(request.plugin(), state, history);
      break;
    case hibiki::pb::commands::Request::kAutomation:
      handleAutomationCmd(request.automation(), state, history);
      break;
    case hibiki::pb::commands::Request::kMidi:
      handleMidiCmd(request.midi(), state, history);
      break;
    case hibiki::pb::commands::Request::kSetPluginHostMode:
      handleSetPluginHostMode(request.set_plugin_host_mode(), state);
      break;
    case hibiki::pb::commands::Request::kScanRemotePlugins:
      handleScanRemotePlugins(request.scan_remote_plugins());
      break;
    case hibiki::pb::commands::Request::kSetAudioBufferSize:
      handleSetAudioBufferSize(request.set_audio_buffer_size(), state);
      break;
    case hibiki::pb::commands::Request::kListAudioInputs:
      handleListAudioInputs();
      break;
    case hibiki::pb::commands::Request::kListMidiInputs:
      handleListMidiInputs();
      break;
    case hibiki::pb::commands::Request::kSendVirtualMidi:
      handleSendVirtualMidi(request.send_virtual_midi(), state);
      break;
    case hibiki::pb::commands::Request::kModulation:
      handleModulationCmd(request.modulation(), state);
      break;
    case hibiki::pb::commands::Request::kRestartWorkers: {
      LOG(INFO) << "Restarting plugin workers...";
      int count = 0;
      for (auto& [idx, track] : state.tracks) {
        for (auto& plugin : track->plugins) {
          if (plugin) {
            std::string path = plugin->getPath();
            int pidx = plugin->getPluginIndex();
            plugin->load(path, pidx, state.sample_rate);
            count++;
          }
        }
      }
      LOG(INFO) << "Reloaded " << count << " plugins";
      break;
    }
    case hibiki::pb::commands::Request::kSetProcessingPrecision:
      handleSetProcessingPrecision(request.set_processing_precision(), state);
      break;
    case hibiki::pb::commands::Request::kAudioEditor:
      handleAudioEditorCmd(request.audio_editor(), state);
      break;
    case hibiki::pb::commands::Request::kDrumPad:
      handleDrumPadCmd(request.drum_pad(), state);
      break;
    case hibiki::pb::commands::Request::COMMAND_NOT_SET:
      break;
  }
  return true;
}

}  // namespace hibiki
