#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// 2nd-order biquad filter section for crossover building blocks.
struct BiquadState {
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

struct BiquadCoeffs {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// Per-band compressor state (upward + downward).
struct BandCompressor {
  float down_threshold_db;  // downward comp threshold
  float down_ratio;         // downward comp ratio (∞ = limiter)
  float up_threshold_db;    // upward comp threshold
  float up_ratio;           // upward comp ratio
  float attack_ms;
  float release_ms;
  float output_gain_db;

  // Envelope follower
  float envelope_db = -100.0f;
  float gain_reduction_db = 0.0f;
};

// OTT-style three-band multiband upward/downward compressor.
// Path: builtin://hott
class BuiltinHott : public IPlugin {
 public:
  static constexpr int kTotalParams = 9;
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

  BandCompressor bands_[kNumBands];  // Low=0, Mid=1, High=2

  std::atomic<float> input_db_{-200.0f};
  std::atomic<float> output_db_{-200.0f};

  void reset();
  void updateCrossoverCoeffs();
  static BiquadCoeffs computeLowpass(float freq, double sr);
  static BiquadCoeffs computeHighpass(float freq, double sr);
  static float processBiquad(BiquadState& state, const BiquadCoeffs& c,
                             float x);
  static float computeBandGain(const BandCompressor& band, float input_db,
                               float amount);
};

}  // namespace hibiki
