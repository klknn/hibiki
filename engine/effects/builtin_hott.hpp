#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/effects/builtin_compressor.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// 2nd-order biquad filter section for crossover building blocks.
struct BiquadState {
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

struct BiquadCoeffs {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

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
    // --- New params (Xfer OTT parity) ---
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

  // IPlugin interface
  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  void showEditor() override {}
  void stopEditor() override {}
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events) override;
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
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  // Crossover filter states (LR4 = 2 cascaded biquads per split)
  // Low/High split: 2 biquads per channel per split = 4 biquad states per split
  BiquadState lp1_L_[2], lp1_R_[2];  // low-pass for low split (2 stages)
  BiquadState hp1_L_[2], hp1_R_[2];  // high-pass for low split
  BiquadState lp2_L_[2], lp2_R_[2];  // low-pass for high split
  BiquadState hp2_L_[2], hp2_R_[2];  // high-pass for high split

  BiquadCoeffs lp1_coeffs_, hp1_coeffs_;
  BiquadCoeffs lp2_coeffs_, hp2_coeffs_;

  BuiltinCompressor band_comp_[kNumBands];  // Low=0, Mid=1, High=2

  std::atomic<float> input_db_{-200.0f};
  std::atomic<float> output_db_{-200.0f};
  int64_t last_time_samples_ = -1;  // For transport discontinuity detection

  void reset();
  void updateBandCompParams();
  void updateCrossoverCoeffs();
  static BiquadCoeffs computeLowpass(float freq, double sr);
  static BiquadCoeffs computeHighpass(float freq, double sr);
  static float processBiquad(BiquadState& state, const BiquadCoeffs& c,
                             float x);
};

}  // namespace hibiki
