#include "engine/core/track.hpp"

#include <algorithm>
#include <iostream>

#include "engine/builtin_registry.hpp"
#include "engine/instruments/builtin_drum_machine.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

std::unique_ptr<IPlugin> Track::CreateAndLoadPlugin(
    const std::string& path, int plugin_index, int track_index,
    double sample_rate, PluginHostMode host_mode,
    const std::string& remote_host) {
  std::unique_ptr<IPlugin> plugin;

  // Built-in devices: create native IPlugin implementations
  if (isBuiltinPluginPath(path)) {
    plugin = createBuiltinPlugin(path);
    if (!plugin) {
      return nullptr;
    }
    if (path == BuiltinDrumMachine::kPath) {
      auto* dm = dynamic_cast<BuiltinDrumMachine*>(plugin.get());
      if (dm) {
        dm->setTrackIndex(track_index);
        dm->setPluginHostMode(host_mode);
      }
    }
    plugin->load(path, 0, sample_rate);
  } else if (!remote_host.empty()) {
    // Per-load remote host — always use TCP proxy
    std::string host = remote_host;
    int port = 9100;
    auto colon = remote_host.rfind(':');
    if (colon != std::string::npos) {
      host = remote_host.substr(0, colon);
      port = std::stoi(remote_host.substr(colon + 1));
    }
    plugin = std::make_unique<PluginProxy>(host, port);
    if (!plugin->load(path, plugin_index, sample_rate)) {
      return nullptr;
    }
  } else {
    // Local plugin — use configured host mode
    switch (host_mode) {
      case PluginHostMode::LOCAL_SANDBOX:
        plugin = std::make_unique<PluginProxy>();
        break;
      case PluginHostMode::IN_PROCESS:
      default:
        plugin = std::make_unique<Vst3Plugin>();
        break;
    }
    if (!plugin->load(path, plugin_index, sample_rate)) {
      return nullptr;
    }
  }
  return plugin;
}

Track::LoadResult Track::InsertLoadedPlugin(std::unique_ptr<IPlugin> plugin) {
  if (!plugin) return {-1, nullptr};
  std::lock_guard<DummyMutex> lock(mutex);

  bool is_instrument = plugin->isInstrument();
  int target_idx = -1;
  if (is_instrument) {
    // Find existing instrument to replace
    for (size_t i = 0; i < plugins.size(); ++i) {
      if (plugins[i]->isInstrument()) {
        target_idx = (int)i;
        break;
      }
    }
  }

  std::unique_ptr<IPlugin> displaced;
  if (target_idx != -1) {
    // Stop playback and editor before replacing plugin
    playing_slot = -1;
    plugins[target_idx]->stopEditor();
    displaced = std::move(plugins[target_idx]);
    plugins[target_idx] = std::move(plugin);
  } else if (is_instrument) {
    // New instrument, insert at 0
    plugins.insert(plugins.begin(), std::move(plugin));
    target_idx = 0;
  } else {
    // Effect, append
    target_idx = (int)plugins.size();
    plugins.push_back(std::move(plugin));
  }

  // If this is the first plugin, reset playback state
  if (plugins.size() == 1) {
    current_time_sec = 0.0;
    current_midi_idx = 0;
  }

  // Exclusivity rule: If loading an instrument, clear audio clips
  if (is_instrument) {
    std::vector<int> audio_slots;
    for (auto const& [slot, clip] : clips) {
      if (clip->type == Clip::Type::AUDIO) audio_slots.push_back(slot);
    }
    for (int slot : audio_slots) {
      clips.erase(slot);
      hibiki::sendClipInfo(index, slot, "", "");
    }
  }

  return {target_idx, std::move(displaced)};
}

Track::LoadResult Track::LoadPlugin(const std::string& path, int plugin_index,
                                    double sample_rate,
                                    PluginHostMode host_mode,
                                    const std::string& remote_host) {
  auto plugin = CreateAndLoadPlugin(path, plugin_index, index, sample_rate,
                                    host_mode, remote_host);
  if (!plugin) return {-1, nullptr};
  return InsertLoadedPlugin(std::move(plugin));
}

bool Track::DeleteClip(int slot) {
  std::lock_guard<DummyMutex> lock(mutex);
  if (clips.count(slot)) {
    clips.erase(slot);
    if (playing_slot == slot) {
      playing_slot = -1;
    }
    return true;
  }
  return false;
}

bool Track::InsertLoadedClip(int slot, std::unique_ptr<Clip> clip,
                             const std::string& path) {
  if (!clip) return false;
  std::lock_guard<DummyMutex> lock(mutex);

  std::string basename = path;
  size_t pos = basename.find_last_of("/\\");
  if (pos != std::string::npos) basename = basename.substr(pos + 1);
  clip->name = basename;

  // Exclusivity rule: If loading an audio clip, clear instruments
  if (clip->type == Clip::Type::AUDIO) {
    for (size_t i = 0; i < plugins.size(); ++i) {
      if (plugins[i]->isInstrument()) {
        hibiki::sendParamList(index, (int)i, "", true, {});
      }
    }
    plugins.erase(
        std::remove_if(plugins.begin(), plugins.end(),
                       [](const auto& p) { return p->isInstrument(); }),
        plugins.end());
  }

  // Send waveform to GUI if generated
  if (clip->type == Clip::Type::AUDIO && !clip->waveform_summary.empty()) {
    hibiki::pb::notifications::Notification notification;
    auto* wf = notification.mutable_clip_waveform();
    wf->set_track_index(index);
    wf->set_slot_index(slot);
    for (float v : clip->waveform_summary) {
      wf->add_waveform(v);
    }
    std::string data;
    notification.SerializeToString(&data);
    hibiki::sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                             data.size());
  }

  clips[slot] = std::move(clip);

  // If we are currently playing this slot, reset playback
  if (playing_slot == slot) {
    current_time_sec = 0.0;
    current_midi_idx = 0;
  }
  return true;
}

bool Track::LoadClip(int slot, const std::string& path, bool is_loop,
                     double sample_rate) {
  auto result = hibiki::LoadClip(path, is_loop, sample_rate);
  if (!result.ok()) return false;
  auto clip = std::make_unique<Clip>(std::move(*result));
  return InsertLoadedClip(slot, std::move(clip), path);
}

void Track::SetClipLoop(int slot, bool is_loop) {
  std::lock_guard<DummyMutex> lock(mutex);
  if (clips.count(slot)) {
    clips[slot]->is_loop = is_loop;
  }
}

void Track::PlayClip(int slot) {
  std::lock_guard<DummyMutex> lock(mutex);
  if (clips.count(slot)) {
    playing_slot = slot;
    current_time_sec = 0.0;
    current_midi_idx = 0;
  }
}

void Track::Stop() {
  std::lock_guard<DummyMutex> lock(mutex);
  playing_slot = -1;
  Panic();
}

void Track::Panic() { panic_requested_ = true; }

std::unique_ptr<IPlugin> Track::RemovePlugin(size_t pidx) {
  std::lock_guard<DummyMutex> lock(mutex);
  if (pidx >= plugins.size()) return nullptr;
  // Stop playback and editor before removing plugin
  playing_slot = -1;
  plugins[pidx]->stopEditor();
  auto removed = std::move(plugins[pidx]);
  plugins.erase(plugins.begin() + pidx);

  const int ipidx = static_cast<int>(pidx);
  std::map<int, bool> new_bypass;
  for (auto& [idx, val] : plugin_bypass) {
    if (idx < ipidx)
      new_bypass[idx] = val;
    else if (idx > ipidx)
      new_bypass[idx - 1] = val;
  }
  plugin_bypass = std::move(new_bypass);

  std::map<int, float> new_mix;
  for (auto& [idx, val] : plugin_mix) {
    if (idx < ipidx)
      new_mix[idx] = val;
    else if (idx > ipidx)
      new_mix[idx - 1] = val;
  }
  plugin_mix = std::move(new_mix);

  std::map<int, SidechainRoute> new_sc;
  for (auto& [idx, val] : plugin_sidechain) {
    if (idx < ipidx)
      new_sc[idx] = val;
    else if (idx > ipidx)
      new_sc[idx - 1] = val;
  }
  plugin_sidechain = std::move(new_sc);
  return removed;
}

void Track::ReorderPlugin(int from_index, int to_index) {
  std::lock_guard<DummyMutex> lock(mutex);
  int n = (int)plugins.size();
  if (from_index < 0 || from_index >= n || to_index < 0 || to_index >= n ||
      from_index == to_index)
    return;

  // Move the plugin
  auto plugin = std::move(plugins[from_index]);
  plugins.erase(plugins.begin() + from_index);
  plugins.insert(plugins.begin() + to_index, std::move(plugin));

  // Remap index-keyed maps (plugin_bypass, plugin_mix, plugin_sidechain)
  // Build a mapping: old_index -> new_index
  auto remap = [&](int old_idx) -> int {
    if (old_idx == from_index) return to_index;
    if (from_index < to_index) {
      // Moved forward: indices in (from, to] shift down by 1
      if (old_idx > from_index && old_idx <= to_index) return old_idx - 1;
    } else {
      // Moved backward: indices in [to, from) shift up by 1
      if (old_idx >= to_index && old_idx < from_index) return old_idx + 1;
    }
    return old_idx;
  };

  std::map<int, bool> new_bypass;
  for (auto& [idx, val] : plugin_bypass) new_bypass[remap(idx)] = val;
  plugin_bypass = std::move(new_bypass);

  std::map<int, float> new_mix;
  for (auto& [idx, val] : plugin_mix) new_mix[remap(idx)] = val;
  plugin_mix = std::move(new_mix);

  std::map<int, SidechainRoute> new_sidechain;
  for (auto& [idx, val] : plugin_sidechain) new_sidechain[remap(idx)] = val;
  plugin_sidechain = std::move(new_sidechain);
}

std::unique_ptr<Clip> Track::PrepareTimelineClip(const std::string& path,
                                                 double duration_beats,
                                                 double sample_rate,
                                                 int& out_pattern_idx) {
  std::unique_ptr<Clip> clip;
  out_pattern_idx = -1;
  if (path.rfind("pattern://", 0) == 0) {
    out_pattern_idx = std::atoi(path.c_str() + 10);
    clip = std::make_unique<Clip>();
    clip->type = Clip::Type::MIDI;
    clip->path = path;
    clip->duration_beats = (duration_beats > 0) ? duration_beats : 4.0;
    clip->duration_sec = 0.0;
  } else if (path.empty()) {
    clip = std::make_unique<Clip>();
    clip->type = Clip::Type::MIDI;
    clip->path = "";
    clip->duration_beats = (duration_beats > 0) ? duration_beats : 4.0;
    clip->duration_sec = 0.0;
  } else {
    auto result = hibiki::LoadClip(path, false, sample_rate);
    if (!result.ok()) return nullptr;
    clip = std::make_unique<Clip>(std::move(*result));
  }
  return clip;
}

void Track::InsertPreparedTimelineClip(std::unique_ptr<Clip> clip,
                                       const std::string& path,
                                       double start_time_sec, double bpm,
                                       int pattern_idx) {
  if (!clip) return;
  std::lock_guard<DummyMutex> lock(mutex);

  std::string basename = (pattern_idx >= 0)
                             ? ("Pattern " + std::to_string(pattern_idx + 1))
                             : (path.empty() ? "New Clip" : path);
  if (pattern_idx < 0) {
    size_t pos = basename.find_last_of("/\\");
    if (pos != std::string::npos) basename = basename.substr(pos + 1);
  }
  clip->name = basename;

  auto tc = std::make_unique<TimelineClip>();
  tc->duration_sec =
      clip->duration_sec;  // In seconds for audio clips, 0 for MIDI
  tc->duration_beats =
      clip->duration_beats;  // In beats for MIDI clips, 0 for audio
  tc->pattern_index = pattern_idx;
  std::vector<float> waveform = clip->waveform_summary;  // copy before move
  const bool is_midi = (clip->type == Clip::Type::MIDI);
  tc->clip = std::move(clip);
  tc->start_time_sec = start_time_sec;
  // For GUI: use duration_sec if set, otherwise convert duration_beats using
  // BPM
  float duration_for_gui = (tc->duration_sec > 0)
                               ? (float)tc->duration_sec
                               : (float)(tc->duration_beats * 60.0 / bpm);
  timeline_clips.push_back(std::move(tc));
  int clip_idx = (int)timeline_clips.size() - 1;
  // Use basename for display name
  hibiki::sendTimelineClipInfo(
      index, clip_idx, basename, path, (float)start_time_sec, duration_for_gui,
      waveform, false, -1, 0.0f, 0.0f, 0.0f, false, pattern_idx, is_midi);
}

void Track::AddTimelineClip(const std::string& path, double start_time_sec,
                            double bpm, double duration_beats,
                            double sample_rate) {
  int pattern_idx = -1;
  auto clip =
      PrepareTimelineClip(path, duration_beats, sample_rate, pattern_idx);
  if (!clip) return;
  InsertPreparedTimelineClip(std::move(clip), path, start_time_sec, bpm,
                             pattern_idx);
}

void Track::RemoveTimelineClip(int clip_index) {
  std::lock_guard<DummyMutex> lock(mutex);
  if (clip_index >= 0 && clip_index < (int)timeline_clips.size()) {
    timeline_clips.erase(timeline_clips.begin() + clip_index);
  }
}

int Track::AddAutomationLane(int plugin_idx, uint32_t param_id) {
  std::lock_guard<DummyMutex> lock(mutex);
  AutomationLane lane;
  lane.plugin_idx = plugin_idx;
  lane.param_id = param_id;
  automation_lanes.push_back(std::move(lane));
  return (int)automation_lanes.size() - 1;
}

bool Track::RemoveAutomationLane(int lane_index) {
  std::lock_guard<DummyMutex> lock(mutex);
  if (lane_index < 0 || lane_index >= (int)automation_lanes.size())
    return false;
  automation_lanes.erase(automation_lanes.begin() + lane_index);
  return true;
}

}  // namespace hibiki
