#include "engine/core/track.hpp"

#include <algorithm>
#include <iostream>

#include "engine/effects/builtin_compressor.hpp"
#include "engine/effects/builtin_delay.hpp"
#include "engine/effects/builtin_eq.hpp"
#include "engine/effects/builtin_hott.hpp"
#include "engine/effects/builtin_limiter.hpp"
#include "engine/effects/builtin_reverb.hpp"
#include "engine/instruments/builtin_3xosc.hpp"
#include "engine/instruments/builtin_sampler.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

std::pair<int, std::unique_ptr<IPlugin>> Track::LoadPlugin(
    const std::string& path, int plugin_index, double sample_rate,
    PluginHostMode host_mode, const std::string& remote_host) {
  std::lock_guard<DummyMutex> lock(mutex);
  std::unique_ptr<IPlugin> plugin;

  // Built-in devices: create native IPlugin implementations
  if (path == BuiltinEq::kPath) {
    plugin = std::make_unique<BuiltinEq>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinCompressor::kPath) {
    plugin = std::make_unique<BuiltinCompressor>();
    plugin->load(path, 0, sample_rate);
  } else if (path == Builtin3xOsc::kPath) {
    plugin = std::make_unique<Builtin3xOsc>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinSampler::kPath) {
    plugin = std::make_unique<BuiltinSampler>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinDelay::kPath) {
    plugin = std::make_unique<BuiltinDelay>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinReverb::kPath) {
    plugin = std::make_unique<BuiltinReverb>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinLimiter::kPath) {
    plugin = std::make_unique<BuiltinLimiter>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinHott::kPath) {
    plugin = std::make_unique<BuiltinHott>();
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
      return {-1, nullptr};
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
      return {-1, nullptr};
    }
  }

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

  std::unique_ptr<IPlugin> old_plugin = nullptr;
  if (target_idx != -1) {
    // Stop playback to prevent audio thread from accessing plugin during
    // replacement
    playing_slot = -1;
    old_plugin = std::move(plugins[target_idx]);
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

  return {target_idx, std::move(old_plugin)};
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

bool Track::LoadClip(int slot, const std::string& path, bool is_loop) {
  std::lock_guard<DummyMutex> lock(mutex);

  auto result = hibiki::LoadClip(path, is_loop);
  if (!result.ok()) return false;
  auto clip = std::make_unique<Clip>(std::move(*result));

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
}

std::unique_ptr<IPlugin> Track::RemovePlugin(size_t pidx) {
  std::lock_guard<DummyMutex> lock(mutex);
  if (pidx >= plugins.size()) return nullptr;
  // Safety: don't allow removing the main instrument at slot 0 via this method.
  if (pidx == 0 && plugins[0]->isInstrument()) return nullptr;
  // Stop playback to prevent audio thread from accessing plugin during
  // destruction
  playing_slot = -1;
  std::unique_ptr<IPlugin> old = std::move(plugins[pidx]);
  plugins.erase(plugins.begin() + pidx);
  return old;
}

void Track::AddTimelineClip(const std::string& path, double start_time_sec,
                            double bpm, double duration_beats) {
  std::lock_guard<DummyMutex> lock(mutex);
  std::unique_ptr<Clip> clip;
  if (path.empty()) {
    // Create an empty MIDI clip for composing from scratch
    clip = std::make_unique<Clip>();
    clip->type = Clip::Type::MIDI;
    clip->path = "";
    clip->duration_beats = (duration_beats > 0) ? duration_beats : 4.0;
    clip->duration_sec = 0.0;
  } else {
    auto result = hibiki::LoadClip(path);
    if (!result.ok()) return;
    clip = std::make_unique<Clip>(std::move(*result));
  }

  auto tc = std::make_unique<TimelineClip>();
  tc->duration_sec =
      clip->duration_sec;  // In seconds for audio clips, 0 for MIDI
  tc->duration_beats =
      clip->duration_beats;  // In beats for MIDI clips, 0 for audio
  std::vector<float> waveform = clip->waveform_summary;  // copy before move
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
  std::string basename = path.empty() ? "New Clip" : path;
  size_t pos = basename.find_last_of("/\\");
  if (pos != std::string::npos) basename = basename.substr(pos + 1);
  hibiki::sendTimelineClipInfo(index, clip_idx, basename, path,
                               (float)start_time_sec, duration_for_gui,
                               waveform);
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
