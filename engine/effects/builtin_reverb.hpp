#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Algorithmic reverb (Freeverb/Schroeder topology).
// 8 parallel comb filters + 4 series allpass filters per channel.
class BuiltinReverb : public IPlugin {
 public:
  static constexpr int kTotalParams = 8;
  static constexpr const char* kPath = "builtin://reverb";
  static constexpr const char* kName = "Reverb";

  enum ParamId {
    PARAM_ROOM_SIZE = 0,
    PARAM_DAMPING = 1,
    PARAM_MIX = 2,
    PARAM_PRE_DELAY = 3,
    PARAM_HP_FREQ = 4,
    PARAM_LP_FREQ = 5,
    PARAM_WIDTH = 6,
    PARAM_ENABLE = 7,
  };

  BuiltinReverb();
  ~BuiltinReverb() override;

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

  // Parameter mapping
  static float normToPreDelayMs(double norm);

  // Metering (read from UI thread)
  float getInputDb() const;
  float getOutputDb() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
