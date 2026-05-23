#include "engine/core/track.hpp"

#include <algorithm>
#include <iostream>

#include "engine/effects/builtin_aux.hpp"
#include "engine/effects/builtin_bitcrusher.hpp"
#include "engine/effects/builtin_chorus.hpp"
#include "engine/effects/builtin_compressor.hpp"
#include "engine/effects/builtin_convolver.hpp"
#include "engine/effects/builtin_delay.hpp"
#include "engine/effects/builtin_envelope_shaper.hpp"
#include "engine/effects/builtin_eq.hpp"
#include "engine/effects/builtin_hott.hpp"
#include "engine/effects/builtin_limiter.hpp"
#include "engine/effects/builtin_maxim.hpp"
#include "engine/effects/builtin_phaser.hpp"
#include "engine/effects/builtin_reverb.hpp"
#include "engine/effects/builtin_stereo_width.hpp"
#include "engine/instruments/builtin_3xosc.hpp"
#include "engine/instruments/builtin_acid_bass.hpp"
#include "engine/instruments/builtin_dr8_hat.hpp"
#include "engine/instruments/builtin_dr8_kick.hpp"
#include "engine/instruments/builtin_dr8_snare.hpp"
#include "engine/instruments/builtin_dr8_tom.hpp"
#include "engine/instruments/builtin_drum_machine.hpp"
#include "engine/instruments/builtin_film.hpp"
#include "engine/instruments/builtin_organ.hpp"
#include "engine/instruments/builtin_sampler.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

Track::LoadResult Track::LoadPlugin(const std::string& path, int plugin_index,
                                    double sample_rate,
                                    PluginHostMode host_mode,
                                    const std::string& remote_host) {
  std::lock_guard<DummyMutex> lock(mutex);
  std::unique_ptr<IPlugin> plugin;

  // Built-in devices: create native IPlugin implementations
  if (path == BuiltinAux::kPath) {
    plugin = std::make_unique<BuiltinAux>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinEq::kPath) {
    plugin = std::make_unique<BuiltinEq>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinCompressor::kPath) {
    plugin = std::make_unique<BuiltinCompressor>();
    plugin->load(path, 0, sample_rate);
  } else if (path == Builtin3xOsc::kPath) {
    plugin = std::make_unique<Builtin3xOsc>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinAcidBass::kPath) {
    plugin = std::make_unique<BuiltinAcidBass>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinDr8Kick::kPath) {
    plugin = std::make_unique<BuiltinDr8Kick>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinDr8Snare::kPath) {
    plugin = std::make_unique<BuiltinDr8Snare>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinDr8Hat::kPath) {
    plugin = std::make_unique<BuiltinDr8Hat>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinDr8Tom::kPath) {
    plugin = std::make_unique<BuiltinDr8Tom>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinOrgan::kPath) {
    plugin = std::make_unique<BuiltinOrgan>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinSampler::kPath) {
    plugin = std::make_unique<BuiltinSampler>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinDrumMachine::kPath) {
    auto dm = std::make_unique<BuiltinDrumMachine>();
    dm->setTrackIndex(index);
    plugin = std::move(dm);
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
  } else if (path == BuiltinBitcrusher::kPath) {
    plugin = std::make_unique<BuiltinBitcrusher>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinChorus::kPath) {
    plugin = std::make_unique<BuiltinChorus>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinStereoWidth::kPath) {
    plugin = std::make_unique<BuiltinStereoWidth>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinHott::kPath) {
    plugin = std::make_unique<BuiltinHott>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinMaxim::kPath) {
    plugin = std::make_unique<BuiltinMaxim>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinEnvelopeShaper::kPath) {
    plugin = std::make_unique<BuiltinEnvelopeShaper>();
    plugin->load(path, 0, sample_rate);
  } else if (path == BuiltinPhaser::kPath) {
    plugin = std::make_unique<BuiltinPhaser>();
    plugin->load(path, 0, sample_rate);
  } else if (path.rfind(BuiltinConvolver::kPath, 0) == 0) {
    plugin = std::make_unique<BuiltinConvolver>();
    plugin->load(path, 0, sample_rate);
  } else if (path.rfind(BuiltinFilm::kPath, 0) == 0) {
    plugin = std::make_unique<BuiltinFilm>();
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

bool Track::LoadClip(int slot, const std::string& path, bool is_loop,
                     double sample_rate) {
  std::lock_guard<DummyMutex> lock(mutex);

  auto result = hibiki::LoadClip(path, is_loop, sample_rate);
  if (!result.ok()) return false;
  auto clip = std::make_unique<Clip>(std::move(*result));
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

  // Remap index-keyed maps (plugin_bypass, plugin_sidechain)
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

  std::map<int, SidechainRoute> new_sidechain;
  for (auto& [idx, val] : plugin_sidechain) new_sidechain[remap(idx)] = val;
  plugin_sidechain = std::move(new_sidechain);
}

void Track::AddTimelineClip(const std::string& path, double start_time_sec,
                            double bpm, double duration_beats,
                            double sample_rate) {
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
    auto result = hibiki::LoadClip(path, false, sample_rate);
    if (!result.ok()) return;
    clip = std::make_unique<Clip>(std::move(*result));
  }

  std::string basename = path.empty() ? "New Clip" : path;
  size_t pos = basename.find_last_of("/\\");
  if (pos != std::string::npos) basename = basename.substr(pos + 1);
  clip->name = basename;

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
