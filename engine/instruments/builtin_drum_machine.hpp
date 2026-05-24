#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "engine/plugin/iplugin.hpp"
#include "engine/plugin/plugin_proxy.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

class BuiltinDrumMachine : public IPlugin {
 public:
  static constexpr const char* kPath = "builtin://drum_machine";
  static constexpr const char* kName = "Drum Machine";
  static constexpr int kNumPads = 64;

  struct PadEffect {
    std::unique_ptr<IPlugin> plugin;
    std::string path;
  };

  struct Pad {
    std::unique_ptr<IPlugin> plugin;
    std::string plugin_path;
    std::vector<PadEffect> effects;
    float volume = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    std::string sample_path;
    std::string sample_name;
    std::vector<float> sample_waveform;
    uint32_t trigger_note = 60;
  };

  BuiltinDrumMachine();
  ~BuiltinDrumMachine() override;

  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;

  void showEditor() override {}
  void stopEditor() override {}

  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events,
               float** sidechain = nullptr) override;

  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double valueNormalized) override;
  double getParameterValue(uint32_t id) const override;

  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;

  // Custom DrumPad commands (called from plugin_commands)
  absl::Status loadPadPlugin(int pad_idx, const std::string& plugin_path);
  absl::Status removePadPlugin(int pad_idx);
  absl::Status loadPadEffect(int pad_idx, int effect_idx,
                             const std::string& effect_path);
  absl::Status removePadEffect(int pad_idx, int effect_idx);
  absl::Status setPadParam(int pad_idx, uint32_t param_id, float value,
                           bool target_effect = false, int effect_idx = 0);
  absl::Status loadPadSample(int pad_idx, const std::string& sample_path);
  absl::Status showPadEditor(int pad_idx, bool target_effect = false,
                             int effect_idx = 0);
  void setPadVolume(int pad_idx, float vol);
  void setPadPan(int pad_idx, float pan);
  void setPadMute(int pad_idx, bool mute);
  void setPadSolo(int pad_idx, bool solo);
  void setPadTriggerNote(int pad_idx, uint32_t note);

  // Serialization helpers
  void serializeState(pb::core::DrumMachineState* state) const;
  void deserializeState(const pb::core::DrumMachineState& state);

  // Send the state of a single pad to the GUI
  void sendPadState(int pad_idx) const;
  void sendAllPadStates() const;

  void setTrackIndex(int idx) { track_index_ = idx; }
  void setPluginHostMode(PluginHostMode mode) { host_mode_ = mode; }

 private:
  double sample_rate_ = 44100.0;
  int plugin_index_ = 0;
  int track_index_ = 0;
  std::string name_ = kName;
  std::string path_ = kPath;

  // Master parameters
  float master_volume_ = 1.0f;
  float master_pan_ = 0.0f;
  bool enabled_ = true;

  Pad pads_[kNumPads];
  mutable std::mutex pads_mutex_;

  // Pre-allocated temporary buffers for mixing
  std::vector<float> temp_left_;
  std::vector<float> temp_right_;
  float* temp_channels_[2];

  std::unique_ptr<IPlugin> createPadPlugin(const std::string& path) const;
  PluginHostMode host_mode_ = PluginHostMode::IN_PROCESS;
};

}  // namespace hibiki
