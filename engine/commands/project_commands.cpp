#include <google/protobuf/text_format.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "absl/log/log.h"
#include "engine/commands/commands.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"

namespace hibiki {

void handleProjectCmd(const pb::commands::ProjectCmd& cmd, ProjectState& state,
                      HistoryManager& history) {
  switch (cmd.action()) {
    case pb::commands::ProjectCmd::ACTION_SAVE: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      // Copy /tmp/hibiki recordings to audio/ subdir next to project file
      std::string project_path = cmd.path();
      std::filesystem::path proj_dir =
          std::filesystem::path(project_path).parent_path();
      state.project_dir = proj_dir.string();
      std::filesystem::path audio_dir = proj_dir / "audio";
      std::filesystem::path tmp_dir = "/tmp/hibiki";
      if (std::filesystem::exists(tmp_dir)) {
        std::filesystem::create_directories(audio_dir);
        for (auto& [tidx, track] : state.tracks) {
          for (auto& tc : track->timeline_clips) {
            if (!tc || !tc->clip) continue;
            std::string cpath = tc->clip->path;
            if (cpath.find("/tmp/hibiki/") == 0) {
              std::filesystem::path src(cpath);
              std::filesystem::path dst = audio_dir / src.filename();
              std::error_code ec;
              std::filesystem::copy_file(
                  src, dst, std::filesystem::copy_options::overwrite_existing,
                  ec);
              if (!ec) {
                tc->clip->path = dst.string();
              }
            }
          }
        }
      }
      auto save_status = SaveProject(state, project_path);
      if (!save_status.ok()) {
        LOG(ERROR) << "Save failed: " << save_status.message();
      }
      sendAck("SAVE_PROJECT", save_status.ok());
      break;
    }
    case pb::commands::ProjectCmd::ACTION_LOAD: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto load_status = LoadProject(state, cmd.path());
      if (!load_status.ok()) {
        LOG(ERROR) << "Load failed: " << load_status.message();
      }
      SyncProjectToGui(state);
      sendAck("LOAD_PROJECT", load_status.ok());
      break;
    }
    case pb::commands::ProjectCmd::ACTION_BOUNCE: {
      std::string path = cmd.path();
      std::thread([&state, path]() { BounceProject(state, path); }).detach();
      break;
    }
    case pb::commands::ProjectCmd::ACTION_UNDO: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      std::vector<uint8_t> current = CaptureProjectState(state);
      std::vector<uint8_t> prev;
      if (history.undo(current, prev)) {
        ApplyProjectState(state, prev);
        SyncProjectToGui(state);
        sendAck("UNDO", true);
      } else {
        sendAck("UNDO", false);
      }
      break;
    }
    case pb::commands::ProjectCmd::ACTION_REDO: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      std::vector<uint8_t> current = CaptureProjectState(state);
      std::vector<uint8_t> next;
      if (history.redo(current, next)) {
        ApplyProjectState(state, next);
        SyncProjectToGui(state);
        sendAck("REDO", true);
      } else {
        sendAck("REDO", false);
      }
      break;
    }
    case pb::commands::ProjectCmd::ACTION_QUIT: {
      state.quit = true;
      break;
    }
    case pb::commands::ProjectCmd::ACTION_SET_BPM: {
      state.bpm = cmd.bpm();
      sendAck("SET_BPM", true);
      break;
    }
    default:
      break;
  }
}

void loadConfig(ProjectState& state) {
  std::ifstream in(kConfigFile);
  if (!in.is_open()) {
    LOG(INFO) << "No config file found (" << kConfigFile << "), using defaults";
    return;
  }
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  pb::commands::HibikiConfig config;
  if (!google::protobuf::TextFormat::ParseFromString(content, &config)) {
    LOG(ERROR) << "Failed to parse " << kConfigFile << ", using defaults";
    return;
  }
  // Apply config to state
  state.plugin_host_mode =
      (config.plugin_host_mode() == pb::commands::PLUGIN_HOST_LOCAL_SANDBOX)
          ? PluginHostMode::LOCAL_SANDBOX
          : PluginHostMode::IN_PROCESS;
  state.remote_hosts.clear();
  for (const auto& host : config.remote_hosts()) {
    state.remote_hosts.push_back(host);
  }
  if (config.buffer_latency_ms() > 0) {
    state.buffer_latency_ms = config.buffer_latency_ms();
  }
  state.use_double_precision = config.use_double_precision();
  LOG(INFO) << "Loaded config from " << kConfigFile;
}

void saveConfig(const ProjectState& state) {
  pb::commands::HibikiConfig config;
  config.set_plugin_host_mode(
      (state.plugin_host_mode == PluginHostMode::LOCAL_SANDBOX)
          ? pb::commands::PLUGIN_HOST_LOCAL_SANDBOX
          : pb::commands::PLUGIN_HOST_IN_PROCESS);
  for (const auto& host : state.remote_hosts) {
    config.add_remote_hosts(host);
  }
  config.set_buffer_latency_ms(state.buffer_latency_ms);
  config.set_use_double_precision(state.use_double_precision);

  std::string text;
  google::protobuf::TextFormat::PrintToString(config, &text);
  std::ofstream out(kConfigFile);
  if (out.is_open()) {
    out << text;
    LOG(INFO) << "Saved config to " << kConfigFile;
  } else {
    LOG(ERROR) << "Failed to save config to " << kConfigFile;
  }
}

void handleSetAudioBufferSize(const pb::commands::SetAudioBufferSize& cmd,
                              ProjectState& state) {
  int ms = cmd.buffer_size_ms();
  if (ms < 10) ms = 10;
  if (ms > 2000) ms = 2000;
  state.buffer_latency_ms = ms;
  LOG(INFO) << "Audio buffer size set to " << ms << " ms (restart to apply)";
  sendAck("SET_AUDIO_BUFFER_SIZE", true);
  saveConfig(state);
}

void handleSetProcessingPrecision(
    const pb::commands::SetProcessingPrecision& cmd, ProjectState& state) {
  state.use_double_precision = cmd.use_double();
  LOG(INFO) << "Processing precision set to "
            << (state.use_double_precision ? "64-bit double" : "32-bit float");
  sendAck("SET_PROCESSING_PRECISION", true);
  saveConfig(state);
}

}  // namespace hibiki
