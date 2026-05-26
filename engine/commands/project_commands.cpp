#include <google/protobuf/text_format.h>

#include <algorithm>
#include <cmath>
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
#include "pb/notifications.pb.h"

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
      // Notify GUI of project directory
      {
        pb::notifications::Notification n;
        auto* pi = n.mutable_project_info();
        pi->set_project_dir(state.project_dir);
        std::string data;
        n.SerializeToString(&data);
        sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                         data.size());
      }
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
      // Notify GUI of project directory
      if (!state.project_dir.empty()) {
        pb::notifications::Notification n;
        auto* pi = n.mutable_project_info();
        pi->set_project_dir(state.project_dir);
        std::string data;
        n.SerializeToString(&data);
        sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                         data.size());
      }
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
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      double old_bpm = state.bpm;
      double new_bpm = cmd.bpm();
      if (new_bpm > 0 && !std::isnan(old_bpm) && old_bpm > 0 && old_bpm != new_bpm) {
        state.bpm = new_bpm;
        double scale = old_bpm / new_bpm;
        for (auto& [tidx, track] : state.tracks) {
          for (auto& tc : track->timeline_clips) {
            tc->start_time_sec *= scale;
          }
          for (auto& lane : track->automation_lanes) {
            for (auto& tc : lane.clips) {
              tc->start_time_sec *= scale;
            }
          }
        }
        state.playhead_pos_sec *= scale;
        state.record_start_sec *= scale;
        state.loop_start_sec *= scale;
        state.loop_end_sec *= scale;
        SyncProjectToGui(state);
      } else if (new_bpm > 0) {
        state.bpm = new_bpm;
      }
      sendAck("SET_BPM", true);
      break;
    }
    case pb::commands::ProjectCmd::ACTION_COLLECT_FILES: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.project_dir.empty()) {
        sendAck("COLLECT_FILES", false);
        break;
      }
      namespace fs = std::filesystem;
      fs::path proj_dir(state.project_dir);
      fs::path audio_dir = proj_dir / "audio";
      fs::path midi_dir = proj_dir / "midi";
      fs::create_directories(audio_dir);
      fs::create_directories(midi_dir);

      int collected = 0;
      auto collectClipPath = [&](std::string& cpath) {
        if (cpath.empty()) return;
        fs::path src(cpath);
        // Skip if already inside project directory
        std::error_code ec;
        auto rel = fs::relative(src, proj_dir, ec);
        if (!ec && !rel.empty() &&
            rel.string().find("..") == std::string::npos) {
          return;  // Already under project dir
        }
        if (!fs::exists(src)) return;

        // Determine target subdir based on extension
        std::string ext = src.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        fs::path target_dir;
        if (ext == ".wav" || ext == ".flac" || ext == ".aiff" ||
            ext == ".ogg" || ext == ".mp3") {
          target_dir = audio_dir;
        } else if (ext == ".mid" || ext == ".midi") {
          target_dir = midi_dir;
        } else {
          target_dir = audio_dir;  // Default to audio
        }

        // Avoid name collision with numeric suffix
        fs::path dst = target_dir / src.filename();
        int suffix = 1;
        while (fs::exists(dst)) {
          std::string stem = src.stem().string();
          std::string new_name = stem + "_" + std::to_string(suffix) + ext;
          dst = target_dir / new_name;
          suffix++;
        }

        std::error_code copy_ec;
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, copy_ec);
        if (!copy_ec) {
          cpath = dst.string();
          collected++;
        }
      };

      for (auto& [tidx, track] : state.tracks) {
        // Timeline clips
        for (auto& tc : track->timeline_clips) {
          if (!tc || !tc->clip) continue;
          collectClipPath(tc->clip->path);
        }
        // Session clips
        for (auto& [cidx, clip] : track->clips) {
          if (!clip) continue;
          collectClipPath(clip->path);
        }
      }

      LOG(INFO) << "Collected " << collected << " files into "
                << proj_dir.string();
      sendAck("COLLECT_FILES", true);
      // Send project info to refresh browser
      {
        pb::notifications::Notification n;
        auto* pi = n.mutable_project_info();
        pi->set_project_dir(state.project_dir);
        std::string data;
        n.SerializeToString(&data);
        sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                         data.size());
      }
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
    state.buffer_latency_ms = std::max(10, config.buffer_latency_ms());
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
