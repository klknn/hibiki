#pragma once

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Stereo compressor with hard/soft knee, implemented as an IPlugin.
// Features: adjustable threshold, ratio, attack, release, knee width,
// makeup gain. Provides gain reduction metering for UI.
class BuiltinCompressor : public IPlugin {
 public:
  static constexpr int kTotalParams = 7;
  static constexpr const char* kPath = "builtin://compressor";
  static constexpr const char* kName = "Compressor";

  // Parameter IDs
  enum ParamId {
    PARAM_THRESHOLD = 0,  // -60..0 dB -> 0..1
    PARAM_RATIO = 1,      // 1..inf -> 0..1  (0=1:1, 1=inf:1)
    PARAM_ATTACK = 2,     // 0.1..100 ms -> 0..1 (log)
    PARAM_RELEASE = 3,    // 10..1000 ms -> 0..1 (log)
    PARAM_KNEE = 4,       // 0..30 dB -> 0..1
    PARAM_MAKEUP = 5,     // 0..30 dB -> 0..1
    PARAM_ENABLE = 6,     // 0/1
  };

  BuiltinCompressor() { reset(); }

  bool load(const std::string& /*path*/, int /*plugin_index*/ = 0,
            double sample_rate = 44100.0) override {
    sample_rate_ = sample_rate;
    reset();
    return true;
  }

  void showEditor() override {}
  void stopEditor() override {}

  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& /*events*/) override {
    if (sample_rate_ != context.sampleRate) {
      sample_rate_ = context.sampleRate;
    }

    float* outL = outputs[0];
    float* outR = outputs[1];
    if (inputs && inputs != outputs) {
      for (int i = 0; i < num_samples; ++i) {
        outL[i] = inputs[0][i];
        outR[i] = inputs[1][i];
      }
    }

    if (!enabled_) {
      gain_reduction_db_ = 0.0f;
      return;
    }

    float threshold = normToThreshold(params_[PARAM_THRESHOLD]);
    float ratio = normToRatio(params_[PARAM_RATIO]);
    float attack_ms = normToAttack(params_[PARAM_ATTACK]);
    float release_ms = normToRelease(params_[PARAM_RELEASE]);
    float knee_db = (float)(params_[PARAM_KNEE] * 30.0);
    float makeup_lin =
        (float)std::pow(10.0, params_[PARAM_MAKEUP] * 30.0 / 20.0);

    float attack_coeff =
        (float)std::exp(-1.0 / (attack_ms * 0.001 * sample_rate_));
    float release_coeff =
        (float)std::exp(-1.0 / (release_ms * 0.001 * sample_rate_));

    float peak_gr = 0.0f;
    float peak_input_db = -200.0f;
    float peak_output_db = -200.0f;

    for (int i = 0; i < num_samples; ++i) {
      // Peak detection (stereo linked)
      float input_abs = std::max(std::abs(outL[i]), std::abs(outR[i]));
      float input_db =
          (input_abs > 1e-10f) ? 20.0f * std::log10(input_abs) : -200.0f;
      if (input_db > peak_input_db) peak_input_db = input_db;

      // Gain computation with soft knee
      float gr_db = computeGainReduction(input_db, threshold, ratio, knee_db);

      // Envelope follower (smooth the gain reduction)
      if (gr_db < envelope_db_) {
        envelope_db_ = attack_coeff * envelope_db_ + (1 - attack_coeff) * gr_db;
      } else {
        envelope_db_ =
            release_coeff * envelope_db_ + (1 - release_coeff) * gr_db;
      }

      float gain = (float)std::pow(10.0, envelope_db_ / 20.0) * makeup_lin;
      outL[i] *= gain;
      outR[i] *= gain;

      // Track output level
      float output_abs = std::max(std::abs(outL[i]), std::abs(outR[i]));
      float output_db =
          (output_abs > 1e-10f) ? 20.0f * std::log10(output_abs) : -200.0f;
      if (output_db > peak_output_db) peak_output_db = output_db;

      if (envelope_db_ < peak_gr) peak_gr = envelope_db_;
    }

    gain_reduction_db_ = peak_gr;
    input_db_.store(peak_input_db, std::memory_order_relaxed);
    output_db_.store(peak_output_db, std::memory_order_relaxed);
  }

  int getParameterCount() const override { return kTotalParams; }

  bool getParameterInfo(int index, VstParamInfo& info) const override {
    if (index < 0 || index >= kTotalParams) return false;
    info.id = index;
    static const char* names[] = {"Threshold", "Ratio",  "Attack", "Release",
                                  "Knee",      "Makeup", "Enable"};
    static const double defaults[] = {1.0, 0.0, 0.3, 0.3, 0.0, 0.0, 1.0};
    info.name = names[index];
    info.defaultValue = defaults[index];
    return true;
  }

  void setParameterValue(uint32_t id, double value) override {
    if (id >= kTotalParams) return;
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }

  double getParameterValue(uint32_t id) const override {
    if (id >= kTotalParams) return 0.0;
    return params_[id];
  }

  const std::string& getName() const override {
    static const std::string name = kName;
    return name;
  }

  const std::string& getPath() const override {
    static const std::string path = kPath;
    return path;
  }

  int getPluginIndex() const override { return 0; }
  bool isInstrument() const override { return false; }

  // For UI metering
  float getGainReductionDb() const { return gain_reduction_db_; }
  float getInputDb() const { return input_db_.load(std::memory_order_relaxed); }
  float getOutputDb() const {
    return output_db_.load(std::memory_order_relaxed);
  }

  // For UI transfer curve rendering
  // Given input in dB, compute output in dB
  float computeOutputDb(float input_db) const {
    float threshold = normToThreshold(params_[PARAM_THRESHOLD]);
    float ratio = normToRatio(params_[PARAM_RATIO]);
    float knee_db = (float)(params_[PARAM_KNEE] * 30.0);
    float gr = computeGainReduction(input_db, threshold, ratio, knee_db);
    return input_db + gr;
  }

 private:
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;
  float envelope_db_ = 0.0f;
  float gain_reduction_db_ = 0.0f;
  std::atomic<float> input_db_{-200.0f};
  std::atomic<float> output_db_{-200.0f};

  void reset() {
    params_[PARAM_THRESHOLD] = 1.0;  // 0 dB (no compression)
    params_[PARAM_RATIO] = 0.0;      // 1:1
    params_[PARAM_ATTACK] = 0.3;
    params_[PARAM_RELEASE] = 0.3;
    params_[PARAM_KNEE] = 0.0;  // hard knee
    params_[PARAM_MAKEUP] = 0.0;
    params_[PARAM_ENABLE] = 1.0;
    enabled_ = true;
    envelope_db_ = 0.0f;
    gain_reduction_db_ = 0.0f;
  }

  // Soft-knee gain reduction computation
  static float computeGainReduction(float input_db, float threshold,
                                    float ratio, float knee_db) {
    float half_knee = knee_db / 2.0f;
    float gr_db = 0.0f;

    if (knee_db <= 0.01f) {
      // Hard knee
      if (input_db > threshold) {
        gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
      }
    } else {
      // Soft knee
      float lower = threshold - half_knee;
      float upper = threshold + half_knee;
      if (input_db <= lower) {
        gr_db = 0.0f;
      } else if (input_db >= upper) {
        gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
      } else {
        // Quadratic interpolation in the knee region
        float x = input_db - lower;
        gr_db = -(1.0f - 1.0f / ratio) * x * x / (2.0f * knee_db);
      }
    }
    return gr_db;
  }

  // Parameter mapping functions
  static float normToThreshold(double norm) {
    return (float)(norm * 60.0 - 60.0);  // 0->-60dB, 1->0dB
  }
  static float normToRatio(double norm) {
    // 0 -> 1:1, 0.5 -> ~4:1, 1.0 -> inf:1
    if (norm >= 0.999) return 1000.0f;  // effectively inf
    return 1.0f / (1.0f - (float)norm);
  }
  static float normToAttack(double norm) {
    return (float)(0.1 * std::pow(1000.0, norm));  // 0.1..100 ms
  }
  static float normToRelease(double norm) {
    return (float)(10.0 * std::pow(100.0, norm));  // 10..1000 ms
  }
};

}  // namespace hibiki
