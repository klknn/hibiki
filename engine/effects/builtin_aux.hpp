#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Aux return device: stereo gain + pan utility for aux bus tracks.
// Placed on Aux tracks to control the return level before further effects.
class BuiltinAux : public IPlugin {
 public:
  static constexpr int kTotalParams = 2;
  static constexpr const char* kPath = "builtin://aux";
  static constexpr const char* kName = "Aux";

  enum ParamId {
    PARAM_GAIN = 0,  // 0.0–1.0, mapped to -inf..+6 dB
    PARAM_PAN = 1,   // 0.0–1.0, center = 0.5
  };

  BuiltinAux();

  // IPlugin interface
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
  void setParameterValue(uint32_t id, double value) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;

 private:
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
};

}  // namespace hibiki
