#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// OTT-style three-band multiband upward/downward compressor.
// Path: builtin://hott
class BuiltinHott : public IPlugin {
 public:
  static constexpr int kTotalParams = 32;
  static constexpr const char* kPath = "builtin://hott";
  static constexpr const char* kName = "Hott";
  static constexpr int kNumBands = 3;

  enum ParamId {
    PARAM_LOW_CROSSOVER = 0,
    PARAM_HIGH_CROSSOVER = 1,
    PARAM_AMOUNT = 2,
    PARAM_TIME = 3,
    PARAM_OUTPUT = 4,
    PARAM_LOW_OUT = 5,
    PARAM_MID_OUT = 6,
    PARAM_HIGH_OUT = 7,
    PARAM_ENABLE = 8,
    PARAM_LOW_DOWN_THRESH = 9,
    PARAM_MID_DOWN_THRESH = 10,
    PARAM_HIGH_DOWN_THRESH = 11,
    PARAM_LOW_UP_THRESH = 12,
    PARAM_MID_UP_THRESH = 13,
    PARAM_HIGH_UP_THRESH = 14,
    PARAM_SOFT_KNEE = 15,
    PARAM_RMS_MODE = 16,
    PARAM_LOW_ATTACK = 17,
    PARAM_MID_ATTACK = 18,
    PARAM_HIGH_ATTACK = 19,
    PARAM_LOW_RELEASE = 20,
    PARAM_MID_RELEASE = 21,
    PARAM_HIGH_RELEASE = 22,
    PARAM_LOW_DOWN_RATIO = 23,
    PARAM_MID_DOWN_RATIO = 24,
    PARAM_HIGH_DOWN_RATIO = 25,
    PARAM_LOW_UP_RATIO = 26,
    PARAM_MID_UP_RATIO = 27,
    PARAM_HIGH_UP_RATIO = 28,
    PARAM_LOW_IN = 29,
    PARAM_MID_IN = 30,
    PARAM_HIGH_IN = 31,
  };

  BuiltinHott();
  ~BuiltinHott() override;

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

  // For UI metering (per-band)
  float getBandGainReduction(int band) const;
  float getInputDb() const;
  float getOutputDb() const;

  // Parameter mapping (public for tests)
  static float normToFreq(double norm, float min_hz, float max_hz);
  static float normToDb24(double norm);  // -24..+24 dB

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
