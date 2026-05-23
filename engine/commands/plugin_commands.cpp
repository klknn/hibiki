#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "engine/commands/commands.hpp"
#include "engine/core/track.hpp"
#include "engine/instruments/builtin_drum_machine.hpp"
#include "engine/instruments/builtin_sampler.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/ipc/tcp.hpp"
#include "engine/plugin/plugin_scanner.hpp"
#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"
#include "pb/plugin_worker.pb.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace hibiki {

namespace {
// Convenience: collect all parameter infos into a vector.
std::vector<VstParamInfo> collectParams(const IPlugin& plugin) {
  std::vector<VstParamInfo> params;
  for (int i = 0; i < plugin.getParameterCount(); ++i) {
    VstParamInfo info;
    if (plugin.getParameterInfo(i, info)) params.push_back(info);
  }
  return params;
}
}  // namespace

void handlePluginCmd(const pb::commands::PluginCmd& cmd, ProjectState& state,
                     HistoryManager& history) {
  int tidx = cmd.target().track_index();
  switch (cmd.action()) {
    case pb::commands::PluginCmd::ACTION_LOAD: {
      std::string vpath = cmd.path();
      int pidx = cmd.target().plugin_index();
      // Displaced plugin must be destroyed OUTSIDE tracks_mutex to avoid
      // blocking the audio thread during VST3 teardown (editor thread join,
      // COM release, etc.)
      std::unique_ptr<IPlugin> displaced;
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        history.pushState(CaptureProjectState(state));
        auto track = GetOrCreateTrack(state, tidx);
        LOG(INFO) << "BACKEND: Loading plugin: " << vpath;
        sendLog("Loading plugin: " + vpath + " ...");
        auto result =
            track->LoadPlugin(vpath, pidx, state.sample_rate,
                              state.plugin_host_mode, cmd.remote_host());
        displaced = std::move(result.displaced);
        int target_idx = result.index;
        if (target_idx != -1) {
          auto& plugin = track->plugins[target_idx];
          sendParamList(tidx, target_idx, plugin->getName(),
                        plugin->isInstrument(), collectParams(*plugin));
          // Push all current param values to UI so knobs reflect
          // the state set during load() (e.g. DX7 preset import).
          for (int i = 0; i < plugin->getParameterCount(); ++i) {
            sendParamValueChange(tidx, target_idx, i,
                                 plugin->getParameterValue(i));
          }
          if (auto* dm = dynamic_cast<BuiltinDrumMachine*>(plugin.get())) {
            dm->sendAllPadStates();
          }
          // If instrument was inserted at front, re-send param lists for all
          // shifted effect plugins so Java panel indices stay in sync.
          if (plugin->isInstrument() && !displaced) {
            for (int i = 0; i < (int)track->plugins.size(); ++i) {
              if (i == target_idx) continue;
              auto& p = track->plugins[i];
              sendParamList(tidx, i, p->getName(), p->isInstrument(),
                            collectParams(*p));
            }
          }
        } else {
          sendLog("Failed to load plugin: " + vpath);
        }
      }
      // `displaced` destroyed here, outside the mutex
      break;
    }
    case pb::commands::PluginCmd::ACTION_REMOVE: {
      int pidx = cmd.target().plugin_index();
      std::unique_ptr<IPlugin> removed;
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        history.pushState(CaptureProjectState(state));
        auto track = GetOrCreateTrack(state, tidx);
        removed = track->RemovePlugin(pidx);
        sendAck("REMOVE_PLUGIN", removed != nullptr);
        if (removed) {
          // If we removed an instrument, close MIDI input so track
          // becomes a pure audio/effects track.
          if (removed->isInstrument()) {
            track->midi_input_device.reset();
          }
          // Notify Java: send empty param list for removed index
          sendParamList(tidx, pidx, "", false, {});
          // Re-send param lists for all remaining plugins at their new indices
          for (int i = 0; i < (int)track->plugins.size(); ++i) {
            auto& p = track->plugins[i];
            sendParamList(tidx, i, p->getName(), p->isInstrument(),
                          collectParams(*p));
          }
        }
      }
      // `removed` destroyed here, outside the mutex
      break;
    }
    case pb::commands::PluginCmd::ACTION_SHOW_GUI: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          plugins[pidx]->showEditor();
          sendAck("SHOW_PLUGIN_GUI", true);
        } else
          sendAck("SHOW_PLUGIN_GUI", false);
      } else
        sendAck("SHOW_PLUGIN_GUI", false);
      break;
    }
    case pb::commands::PluginCmd::ACTION_SET_PARAM: {
      int pidx = cmd.target().plugin_index();
      uint32_t param_id = cmd.param_id();
      float value = cmd.param_value();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          plugins[pidx]->setParameterValue(param_id, value);
          sendParamValueChange(tidx, pidx, param_id, value);
        }
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_LIST: {
      // Batch mode: scan multiple bundles in parallel
      if (cmd.paths_size() > 0) {
        std::vector<std::string> bundles;
        for (const auto& p : cmd.paths()) {
          bundles.push_back(p);
        }
        std::thread([bundles]() {
          scanBundlesParallel(
              bundles, Vst3Plugin::listPluginsIsolated,
              [](const std::string& path,
                 const std::vector<PluginDescription>& plugins) {
                sendPluginList(path, plugins);
              });
        }).detach();
      } else {
        // Single path fallback
        std::string path = cmd.path();
        std::thread([path]() {
          sendPluginList(path, Vst3Plugin::listPluginsIsolated(path));
        }).detach();
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_GET_EDITOR_FRAME: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          std::vector<uint8_t> rgba;
          int w = 0, h = 0;
          if (plugins[pidx]->captureEditorFrame(rgba, w, h)) {
            sendEditorFrameData(tidx, pidx, w, h, rgba);
          }
        }
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_SEND_EDITOR_INPUT: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          plugins[pidx]->sendEditorInput(cmd.input_type(), cmd.input_x(),
                                         cmd.input_y(), cmd.input_button(),
                                         cmd.input_key(), cmd.input_delta());
        }
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_STOP_GUI: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          plugins[pidx]->stopEditor();
          sendAck("STOP_PLUGIN_GUI", true);
        } else
          sendAck("STOP_PLUGIN_GUI", false);
      } else
        sendAck("STOP_PLUGIN_GUI", false);
      break;
    }
    case pb::commands::PluginCmd::ACTION_LOAD_SAMPLE: {
      int pidx = cmd.target().plugin_index();
      std::string sample_path = cmd.sample_path();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          auto* sampler = dynamic_cast<BuiltinSampler*>(plugins[pidx].get());
          if (sampler && sampler->loadSample(sample_path)) {
            pb::notifications::Notification notification;
            auto* sd = notification.mutable_plugin_sample_data();
            sd->set_track_index(tidx);
            sd->set_plugin_index(pidx);
            for (float v : sampler->getWaveformSummary()) {
              sd->add_waveform(v);
            }
            auto slash = sample_path.rfind('/');
            sd->set_sample_name(slash != std::string::npos
                                    ? sample_path.substr(slash + 1)
                                    : sample_path);
            std::string data;
            notification.SerializeToString(&data);
            sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                             data.size());
            sendAck("LOAD_SAMPLE", true);
          } else {
            sendAck("LOAD_SAMPLE", false);
          }
        } else {
          sendAck("LOAD_SAMPLE", false);
        }
      } else {
        sendAck("LOAD_SAMPLE", false);
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_SET_SIDECHAIN: {
      int pidx = cmd.target().plugin_index();
      int sc_tidx = cmd.sidechain_track_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (sc_tidx < 0) {
          track->plugin_sidechain.erase(pidx);
        } else {
          track->plugin_sidechain[pidx] = {sc_tidx};
        }
        sendAck("SET_SIDECHAIN", true);
      } else {
        sendAck("SET_SIDECHAIN", false);
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_SET_BYPASS: {
      int pidx = cmd.target().plugin_index();
      bool bypassed = !cmd.flag();  // flag=true means "on", so bypassed = !on
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (bypassed) {
          track->plugin_bypass[pidx] = true;
        } else {
          track->plugin_bypass.erase(pidx);
        }
        LOG(INFO) << "Plugin " << pidx << " on track " << tidx
                  << (bypassed ? " bypassed" : " enabled");
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_REORDER_PLUGIN: {
      int from_idx = cmd.target().plugin_index();
      int to_idx = cmd.target_plugin_index();
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        history.pushState(CaptureProjectState(state));
        auto track = GetOrCreateTrack(state, tidx);
        track->ReorderPlugin(from_idx, to_idx);
        // Re-send param lists for all plugins at their new indices
        for (int i = 0; i < (int)track->plugins.size(); ++i) {
          auto& p = track->plugins[i];
          sendParamList(tidx, i, p->getName(), p->isInstrument(),
                        collectParams(*p));
        }
      }
      LOG(INFO) << "Reordered plugin on track " << tidx << ": " << from_idx
                << " -> " << to_idx;
      sendAck("REORDER_PLUGIN", true);
      break;
    }
    default:
      break;
  }
}

void handleSetPluginHostMode(const pb::commands::SetPluginHostMode& cmd,
                             ProjectState& state) {
  switch (cmd.mode()) {
    case pb::commands::PLUGIN_HOST_LOCAL_SANDBOX:
      state.plugin_host_mode = PluginHostMode::LOCAL_SANDBOX;
      break;
    case pb::commands::PLUGIN_HOST_IN_PROCESS:
    default:
      state.plugin_host_mode = PluginHostMode::IN_PROCESS;
      break;
  }
  // Always update remote hosts list (independent of local mode)
  state.remote_hosts.clear();
  for (const auto& host : cmd.remote_hosts()) {
    state.remote_hosts.push_back(host);
  }
  sendAck("SET_PLUGIN_HOST_MODE", true);
  saveConfig(state);
}

void handleScanRemotePlugins(const pb::commands::ScanRemotePlugins& cmd) {
  // Query each remote daemon for its plugin list in parallel.
  for (const auto& host_port : cmd.remote_hosts()) {
    std::string hp = host_port;
    std::thread([hp]() {
      std::string host = hp;
      int port = 9100;
      auto colon = hp.rfind(':');
      if (colon != std::string::npos) {
        host = hp.substr(0, colon);
        port = std::stoi(hp.substr(colon + 1));
      }

      tcp_init();

      socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd == INVALID_SOCK) {
        LOG(ERROR) << "ScanRemote: socket() failed for " << hp;
        return;
      }

      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);

      // Resolve hostname
      struct hostent* he = gethostbyname(host.c_str());
      if (!he) {
        LOG(INFO) << "ScanRemote: cannot resolve " << host;
        tcp_close(fd);
        return;
      }
      memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

      if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG(ERROR) << "ScanRemote: connect failed for " << hp;
        tcp_close(fd);
        return;
      }

      // Helper: length-prefixed send/recv
      auto tcpSend = [&](const std::string& data) -> bool {
        uint32_t size = static_cast<uint32_t>(data.size());
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&size);
        size_t rem = sizeof(size);
        while (rem > 0) {
          int n = tcp_send(fd, p, rem);
          if (n <= 0) return false;
          p += n;
          rem -= n;
        }
        p = reinterpret_cast<const uint8_t*>(data.data());
        rem = data.size();
        while (rem > 0) {
          int n = tcp_send(fd, p, rem);
          if (n <= 0) return false;
          p += n;
          rem -= n;
        }
        return true;
      };

      auto tcpRecv = [&](std::string& out) -> bool {
        uint32_t size = 0;
        uint8_t* p = reinterpret_cast<uint8_t*>(&size);
        size_t rem = sizeof(size);
        while (rem > 0) {
          int n = tcp_recv(fd, p, rem);
          if (n <= 0) return false;
          p += n;
          rem -= n;
        }
        if (size > 4 * 1024 * 1024) return false;
        out.resize(size);
        p = reinterpret_cast<uint8_t*>(out.data());
        rem = size;
        while (rem > 0) {
          int n = tcp_recv(fd, p, rem);
          if (n <= 0) return false;
          p += n;
          rem -= n;
        }
        return true;
      };

      // Send ListPlugins request
      pb::worker::WorkerRequest req;
      auto* lp = req.mutable_list_plugins();
      lp->set_search_path("/");  // daemon scans its local paths

      std::string req_data;
      req.SerializeToString(&req_data);
      if (!tcpSend(req_data)) {
        LOG(ERROR) << "ScanRemote: send failed for " << hp;
        tcp_close(fd);
        return;
      }

      // Read streamed plugin chunks until is_complete=true
      int total_plugins = 0;
      while (true) {
        std::string resp_data;
        if (!tcpRecv(resp_data)) {
          LOG(ERROR) << "ScanRemote: recv failed for " << hp;
          break;
        }

        pb::worker::WorkerResponse resp;
        if (!resp.ParseFromString(resp_data) ||
            resp.result_case() !=
                pb::worker::WorkerResponse::kListPluginsResult) {
          LOG(INFO) << "ScanRemote: bad response from " << hp;
          break;
        }

        const auto& chunk = resp.list_plugins_result();

        // Forward this chunk as a notification (even if empty)
        if (chunk.plugins_size() > 0) {
          pb::notifications::PluginListResponse plr;
          plr.set_path("/");
          plr.set_remote_host(hp);
          for (const auto& pi : chunk.plugins()) {
            auto* pd = plr.add_plugins();
            pd->set_index(pi.plugin_index());
            pd->set_name(pi.name());
            pd->set_vendor("");  // worker proto doesn't have vendor
            pd->set_path(pi.path());
          }
          total_plugins += chunk.plugins_size();

          pb::notifications::Notification notif;
          *notif.mutable_plugin_list() = plr;
          std::string notif_data;
          notif.SerializeToString(&notif_data);
          sendNotification(reinterpret_cast<const uint8_t*>(notif_data.data()),
                           notif_data.size());
        }

        if (chunk.is_complete()) break;
      }

      LOG(INFO) << "ScanRemote: found " << total_plugins << " plugins on "
                << hp;

      // Send shutdown to be polite, then close
      pb::worker::WorkerRequest shutdown_req;
      shutdown_req.mutable_shutdown();
      std::string sd;
      shutdown_req.SerializeToString(&sd);
      tcpSend(sd);
      tcp_close(fd);
    }).detach();
  }
  sendAck("SCAN_REMOTE_PLUGINS", true);
}

void handleDrumPadCmd(const pb::commands::DrumPadCmd& cmd,
                      ProjectState& state) {
  int tidx = cmd.track_index();
  int pidx = cmd.plugin_index();
  int pad_idx = cmd.pad_index();

  std::lock_guard<std::mutex> lock(state.tracks_mutex);
  if (!state.tracks.count(tidx)) {
    sendAck("DRUM_PAD_CMD", false);
    return;
  }

  auto& plugins = state.tracks[tidx]->plugins;
  if (pidx < 0 || pidx >= (int)plugins.size()) {
    sendAck("DRUM_PAD_CMD", false);
    return;
  }

  auto* dm = dynamic_cast<BuiltinDrumMachine*>(plugins[pidx].get());
  if (!dm) {
    sendAck("DRUM_PAD_CMD", false);
    return;
  }

  bool success = false;
  switch (cmd.action()) {
    case pb::commands::DrumPadCmd::ACTION_LOAD_PLUGIN:
      success = dm->loadPadPlugin(pad_idx, cmd.plugin_path());
      break;
    case pb::commands::DrumPadCmd::ACTION_REMOVE_PLUGIN:
      success = dm->removePadPlugin(pad_idx);
      break;
    case pb::commands::DrumPadCmd::ACTION_SET_PARAM:
      success = dm->setPadParam(pad_idx, cmd.param_id(), cmd.param_value());
      break;
    case pb::commands::DrumPadCmd::ACTION_LOAD_SAMPLE:
      success = dm->loadPadSample(pad_idx, cmd.sample_path());
      break;
    case pb::commands::DrumPadCmd::ACTION_SET_VOLUME:
      dm->setPadVolume(pad_idx, cmd.param_value());
      success = true;
      break;
    case pb::commands::DrumPadCmd::ACTION_SET_PAN:
      dm->setPadPan(pad_idx, cmd.param_value());
      success = true;
      break;
    case pb::commands::DrumPadCmd::ACTION_SET_MUTE:
      dm->setPadMute(pad_idx, cmd.param_value() >= 0.5f);
      success = true;
      break;
    case pb::commands::DrumPadCmd::ACTION_SET_SOLO:
      dm->setPadSolo(pad_idx, cmd.param_value() >= 0.5f);
      success = true;
      break;
    case pb::commands::DrumPadCmd::ACTION_SET_TRIGGER_NOTE:
      dm->setPadTriggerNote(pad_idx, cmd.trigger_note());
      success = true;
      break;
    default:
      break;
  }

  sendAck("DRUM_PAD_CMD", success);
}

}  // namespace hibiki
