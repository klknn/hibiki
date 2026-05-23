#include "engine/instruments/builtin_drum_machine.hpp"

#include <algorithm>
#include <cmath>

#include "engine/instruments/builtin_3xosc.hpp"
#include "engine/instruments/builtin_sampler.hpp"
#include "engine/ipc/ipc.hpp"

namespace hibiki {

BuiltinDrumMachine::BuiltinDrumMachine() {
  temp_channels_[0] = nullptr;
  temp_channels_[1] = nullptr;
}

BuiltinDrumMachine::~BuiltinDrumMachine() = default;

bool BuiltinDrumMachine::load(const std::string& path, int plugin_index,
                              double sample_rate) {
  std::lock_guard<std::mutex> lock(pads_mutex_);
  path_ = path;
  plugin_index_ = plugin_index;
  sample_rate_ = sample_rate;
  return true;
}

void BuiltinDrumMachine::process(float** /*inputs*/, float** outputs,
                                 int num_samples,
                                 const HostProcessContext& context,
                                 const std::vector<MidiNoteEvent>& events,
                                 float** /*sidechain*/) {
  sample_rate_ = context.sampleRate;

  float* outL = outputs[0];
  float* outR = outputs[1];

  // Initialize output buffer to 0
  for (int i = 0; i < num_samples; ++i) {
    outL[i] = 0.0f;
    outR[i] = 0.0f;
  }

  if (!enabled_) return;

  std::lock_guard<std::mutex> lock(pads_mutex_);

  // Resize temporary buffers if necessary
  if ((int)temp_left_.size() < num_samples) {
    temp_left_.resize(num_samples);
    temp_right_.resize(num_samples);
  }
  temp_channels_[0] = temp_left_.data();
  temp_channels_[1] = temp_right_.data();

  // Determine if any pad is soloed
  bool any_solo = false;
  for (int i = 0; i < kNumPads; ++i) {
    if (pads_[i].plugin && pads_[i].solo) {
      any_solo = true;
      break;
    }
  }

  // Process each pad
  for (int i = 0; i < kNumPads; ++i) {
    auto& pad = pads_[i];

    // Filter MIDI events targeting this pad (note = 36 + i)
    std::vector<MidiNoteEvent> pad_events;
    bool has_note_on = false;
    for (const auto& ev : events) {
      if (ev.pitch == 36 + i) {
        MidiNoteEvent pad_ev = ev;
        pad_ev.pitch = pad.trigger_note;
        pad_events.push_back(pad_ev);
        if (ev.isNoteOn) {
          has_note_on = true;
        }
      }
    }

    if (!pad.plugin) continue;

    // Clear temporary buffers for child plugin
    std::fill(temp_left_.begin(), temp_left_.begin() + num_samples, 0.0f);
    std::fill(temp_right_.begin(), temp_right_.begin() + num_samples, 0.0f);

    // Call child plugin process
    pad.plugin->process(nullptr, temp_channels_, num_samples, context,
                        pad_events);

    // Mix in if active and matching solo/mute rules
    bool play_pad = !pad.mute;
    if (any_solo && !pad.solo) {
      play_pad = false;
    }

    if (play_pad) {
      float vol = pad.volume;
      float pan = pad.pan;
      float left_gain = vol * (1.0f - std::max(0.0f, pan));
      float right_gain = vol * (1.0f - std::max(0.0f, -pan));

      for (int s = 0; s < num_samples; ++s) {
        outL[s] += temp_left_[s] * left_gain;
        outR[s] += temp_right_[s] * right_gain;
      }
    }

    // Send visual trigger flash if note was triggered
    if (has_note_on) {
      pb::notifications::Notification notification;
      auto* dp = notification.mutable_drum_pad();
      dp->set_type(pb::notifications::DrumPadNotification::TYPE_PAD_TRIGGER);
      dp->set_track_index(track_index_);
      dp->set_plugin_index(plugin_index_);
      dp->set_pad_index(i);

      std::string serialized;
      if (notification.SerializeToString(&serialized)) {
        hibiki::sendNotification(
            reinterpret_cast<const uint8_t*>(serialized.data()),
            serialized.size());
      }
    }
  }

  // Apply master volume and pan
  float left_gain = master_volume_ * (1.0f - std::max(0.0f, master_pan_));
  float right_gain = master_volume_ * (1.0f - std::max(0.0f, -master_pan_));
  for (int i = 0; i < num_samples; ++i) {
    outL[i] *= left_gain;
    outR[i] *= right_gain;
  }
}

int BuiltinDrumMachine::getParameterCount() const { return 3; }

bool BuiltinDrumMachine::getParameterInfo(int index, VstParamInfo& info) const {
  if (index == 0) {
    info.id = 0;
    info.name = "Volume";
    info.defaultValue = 1.0;
    return true;
  } else if (index == 1) {
    info.id = 1;
    info.name = "Pan";
    info.defaultValue = 0.0;
    return true;
  } else if (index == 2) {
    info.id = 2;
    info.name = "Enable";
    info.defaultValue = 1.0;
    return true;
  }
  return false;
}

void BuiltinDrumMachine::setParameterValue(uint32_t id,
                                           double valueNormalized) {
  if (id == 0) {
    master_volume_ = (float)valueNormalized;
  } else if (id == 1) {
    master_pan_ = (float)(valueNormalized * 2.0 - 1.0);
  } else if (id == 2) {
    enabled_ = valueNormalized >= 0.5;
  }
}

double BuiltinDrumMachine::getParameterValue(uint32_t id) const {
  if (id == 0) return master_volume_;
  if (id == 1) return (master_pan_ + 1.0) / 2.0;
  if (id == 2) return enabled_ ? 1.0 : 0.0;
  return 0.0;
}

const std::string& BuiltinDrumMachine::getName() const { return name_; }

const std::string& BuiltinDrumMachine::getPath() const { return path_; }

int BuiltinDrumMachine::getPluginIndex() const { return plugin_index_; }

bool BuiltinDrumMachine::isInstrument() const { return true; }

bool BuiltinDrumMachine::loadPadPlugin(int pad_idx,
                                       const std::string& plugin_path) {
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx < 0 || pad_idx >= kNumPads) return false;

    pads_[pad_idx].plugin.reset();
    pads_[pad_idx].plugin_path = plugin_path;
    pads_[pad_idx].sample_path = "";
    pads_[pad_idx].sample_name = "";
    pads_[pad_idx].sample_waveform.clear();

    if (plugin_path == BuiltinSampler::kPath) {
      pads_[pad_idx].plugin = std::make_unique<BuiltinSampler>();
      pads_[pad_idx].plugin->load(plugin_path, 0, sample_rate_);
    } else if (plugin_path == Builtin3xOsc::kPath) {
      pads_[pad_idx].plugin = std::make_unique<Builtin3xOsc>();
      pads_[pad_idx].plugin->load(plugin_path, 0, sample_rate_);
    }
  }

  sendPadState(pad_idx);
  return true;
}

// Wrapper methods that lock pads_mutex_, update state, unlock, and send
// notifications
bool BuiltinDrumMachine::removePadPlugin(int pad_idx) {
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx < 0 || pad_idx >= kNumPads) return false;
    pads_[pad_idx].plugin.reset();
    pads_[pad_idx].plugin_path = "";
    pads_[pad_idx].sample_path = "";
    pads_[pad_idx].sample_name = "";
    pads_[pad_idx].sample_waveform.clear();
  }
  sendPadState(pad_idx);
  return true;
}

bool BuiltinDrumMachine::setPadParam(int pad_idx, uint32_t param_id,
                                     float value) {
  bool ok = false;
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx >= 0 && pad_idx < kNumPads && pads_[pad_idx].plugin) {
      pads_[pad_idx].plugin->setParameterValue(param_id, value);
      ok = true;
    }
  }
  if (ok) {
    sendPadState(pad_idx);
  }
  return ok;
}

bool BuiltinDrumMachine::loadPadSample(int pad_idx,
                                       const std::string& sample_path) {
  bool ok = false;
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx >= 0 && pad_idx < kNumPads) {
      auto* sampler =
          dynamic_cast<BuiltinSampler*>(pads_[pad_idx].plugin.get());
      if (sampler && sampler->loadSample(sample_path)) {
        pads_[pad_idx].sample_path = sample_path;
        auto slash = sample_path.rfind('/');
        pads_[pad_idx].sample_name = (slash != std::string::npos)
                                         ? sample_path.substr(slash + 1)
                                         : sample_path;
        pads_[pad_idx].sample_waveform = sampler->getWaveformSummary();
        ok = true;
      }
    }
  }
  if (ok) {
    sendPadState(pad_idx);
  }
  return ok;
}

void BuiltinDrumMachine::setPadVolume(int pad_idx, float vol) {
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx >= 0 && pad_idx < kNumPads) {
      pads_[pad_idx].volume = vol;
    }
  }
  sendPadState(pad_idx);
}

void BuiltinDrumMachine::setPadPan(int pad_idx, float pan) {
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx >= 0 && pad_idx < kNumPads) {
      pads_[pad_idx].pan = pan;
    }
  }
  sendPadState(pad_idx);
}

void BuiltinDrumMachine::setPadMute(int pad_idx, bool mute) {
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx >= 0 && pad_idx < kNumPads) {
      pads_[pad_idx].mute = mute;
    }
  }
  sendPadState(pad_idx);
}

void BuiltinDrumMachine::setPadSolo(int pad_idx, bool solo) {
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx >= 0 && pad_idx < kNumPads) {
      pads_[pad_idx].solo = solo;
    }
  }
  sendPadState(pad_idx);
}

void BuiltinDrumMachine::setPadTriggerNote(int pad_idx, uint32_t note) {
  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    if (pad_idx >= 0 && pad_idx < kNumPads) {
      pads_[pad_idx].trigger_note = note;
    }
  }
  sendPadState(pad_idx);
}

void BuiltinDrumMachine::serializeState(
    pb::core::DrumMachineState* state) const {
  std::lock_guard<std::mutex> lock(pads_mutex_);
  for (int i = 0; i < kNumPads; ++i) {
    const auto& pad = pads_[i];
    if (pad.plugin_path.empty()) continue;

    auto* pad_state = state->add_pads();
    pad_state->set_pad_index(i);
    pad_state->set_plugin_path(pad.plugin_path);
    pad_state->set_volume(pad.volume);
    pad_state->set_pan(pad.pan);
    pad_state->set_mute(pad.mute);
    pad_state->set_solo(pad.solo);
    pad_state->set_sample_path(pad.sample_path);
    pad_state->set_trigger_note(pad.trigger_note);

    if (pad.plugin) {
      int count = pad.plugin->getParameterCount();
      for (int p = 0; p < count; ++p) {
        VstParamInfo info;
        if (pad.plugin->getParameterInfo(p, info)) {
          double val = pad.plugin->getParameterValue(info.id);
          if (val != info.defaultValue) {
            auto* param = pad_state->add_params();
            param->set_id(info.id);
            param->set_current_value(val);
          }
        }
      }
    }
  }
}

void BuiltinDrumMachine::deserializeState(
    const pb::core::DrumMachineState& state) {
  std::lock_guard<std::mutex> lock(pads_mutex_);

  // Reset all pads
  for (int i = 0; i < kNumPads; ++i) {
    pads_[i].plugin.reset();
    pads_[i].plugin_path = "";
    pads_[i].volume = 1.0f;
    pads_[i].pan = 0.0f;
    pads_[i].mute = false;
    pads_[i].solo = false;
    pads_[i].sample_path = "";
    pads_[i].sample_name = "";
    pads_[i].sample_waveform.clear();
    pads_[i].trigger_note = 60;
  }

  for (const auto& pad_state : state.pads()) {
    int idx = pad_state.pad_index();
    if (idx < 0 || idx >= kNumPads) continue;

    auto& pad = pads_[idx];
    pad.plugin_path = pad_state.plugin_path();
    pad.volume = pad_state.volume();
    pad.pan = pad_state.pan();
    pad.mute = pad_state.mute();
    pad.solo = pad_state.solo();
    pad.sample_path = pad_state.sample_path();
    pad.trigger_note =
        pad_state.has_trigger_note() ? pad_state.trigger_note() : 60;

    if (pad.plugin_path == BuiltinSampler::kPath) {
      auto sampler = std::make_unique<BuiltinSampler>();
      sampler->load(pad.plugin_path, 0, sample_rate_);
      if (!pad.sample_path.empty()) {
        if (sampler->loadSample(pad.sample_path)) {
          auto slash = pad.sample_path.rfind('/');
          pad.sample_name = (slash != std::string::npos)
                                ? pad.sample_path.substr(slash + 1)
                                : pad.sample_path;
          pad.sample_waveform = sampler->getWaveformSummary();
        }
      }
      pad.plugin = std::move(sampler);
    } else if (pad.plugin_path == Builtin3xOsc::kPath) {
      auto osc = std::make_unique<Builtin3xOsc>();
      osc->load(pad.plugin_path, 0, sample_rate_);
      pad.plugin = std::move(osc);
    }

    if (pad.plugin) {
      for (const auto& param_data : pad_state.params()) {
        pad.plugin->setParameterValue(param_data.id(),
                                      param_data.current_value());
      }
    }
  }
}

void BuiltinDrumMachine::sendAllPadStates() const {
  for (int i = 0; i < kNumPads; ++i) {
    sendPadState(i);
  }
}

void BuiltinDrumMachine::sendPadState(int pad_idx) const {
  if (pad_idx < 0 || pad_idx >= kNumPads) return;

  std::string plugin_path;
  float volume;
  float pan;
  bool mute;
  bool solo;
  uint32_t trigger_note;
  std::string sample_name;
  std::vector<float> sample_waveform;
  std::vector<VstParamInfo> param_infos;
  std::vector<double> param_values;

  {
    std::lock_guard<std::mutex> lock(pads_mutex_);
    const auto& pad = pads_[pad_idx];
    plugin_path = pad.plugin_path;
    volume = pad.volume;
    pan = pad.pan;
    mute = pad.mute;
    solo = pad.solo;
    trigger_note = pad.trigger_note;
    sample_name = pad.sample_name;
    sample_waveform = pad.sample_waveform;

    if (pad.plugin) {
      int count = pad.plugin->getParameterCount();
      for (int i = 0; i < count; ++i) {
        VstParamInfo info;
        if (pad.plugin->getParameterInfo(i, info)) {
          param_infos.push_back(info);
          param_values.push_back(pad.plugin->getParameterValue(info.id));
        }
      }
    }
  }

  pb::notifications::Notification notification;
  auto* dp = notification.mutable_drum_pad();
  dp->set_type(pb::notifications::DrumPadNotification::TYPE_PAD_STATE);
  dp->set_track_index(track_index_);
  dp->set_plugin_index(plugin_index_);
  dp->set_pad_index(pad_idx);
  dp->set_plugin_path(plugin_path);
  dp->set_volume(volume);
  dp->set_pan(pan);
  dp->set_mute(mute);
  dp->set_solo(solo);
  dp->set_trigger_note(trigger_note);
  dp->set_sample_name(sample_name);
  for (float v : sample_waveform) {
    dp->add_sample_waveform(v);
  }

  for (size_t i = 0; i < param_infos.size(); ++i) {
    auto* p = dp->add_params();
    p->set_id(param_infos[i].id);
    p->set_name(param_infos[i].name);
    p->set_default_value(param_infos[i].defaultValue);
    p->set_current_value(param_values[i]);
  }

  std::string serialized;
  if (notification.SerializeToString(&serialized)) {
    hibiki::sendNotification(
        reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size());
  }
}

}  // namespace hibiki
