#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/core/biquad_filter.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

class BuiltinMaxim : public IPlugin {
 public:
  static constexpr int kTotalParams = 44;
  static constexpr const char* kPath = "builtin://maxim";
  static constexpr const char* kName = "Maxim";
  static constexpr int kNumBands = 3;         // Low, Mid, High
  static constexpr int kMaxLookahead = 1920;  // 10ms @ 192kHz

  enum ParamId {
    PARAM_LOW_XOVER = 0,
    PARAM_HIGH_XOVER = 1,
    PARAM_LOOKAHEAD = 2,
    PARAM_ENABLE = 3,

    // Low band
    PARAM_LOW_PREGAIN = 4,
    PARAM_LOW_POSTGAIN = 5,
    PARAM_LOW_SAT_AMOUNT = 6,
    PARAM_LOW_SAT_THRESHOLD = 7,
    PARAM_LOW_THRESH = 8,
    PARAM_LOW_RATIO = 9,
    PARAM_LOW_KNEE = 10,
    PARAM_LOW_ATTACK = 11,
    PARAM_LOW_RELEASE = 12,
    PARAM_LOW_CEILING = 13,

    // Mid band
    PARAM_MID_PREGAIN = 14,
    PARAM_MID_POSTGAIN = 15,
    PARAM_MID_SAT_AMOUNT = 16,
    PARAM_MID_SAT_THRESHOLD = 17,
    PARAM_MID_THRESH = 18,
    PARAM_MID_RATIO = 19,
    PARAM_MID_KNEE = 20,
    PARAM_MID_ATTACK = 21,
    PARAM_MID_RELEASE = 22,
    PARAM_MID_CEILING = 23,

    // High band
    PARAM_HIGH_PREGAIN = 24,
    PARAM_HIGH_POSTGAIN = 25,
    PARAM_HIGH_SAT_AMOUNT = 26,
    PARAM_HIGH_SAT_THRESHOLD = 27,
    PARAM_HIGH_THRESH = 28,
    PARAM_HIGH_RATIO = 29,
    PARAM_HIGH_KNEE = 30,
    PARAM_HIGH_ATTACK = 31,
    PARAM_HIGH_RELEASE = 32,
    PARAM_HIGH_CEILING = 33,

    // Master band
    PARAM_MASTER_PREGAIN = 34,
    PARAM_MASTER_POSTGAIN = 35,
    PARAM_MASTER_SAT_AMOUNT = 36,
    PARAM_MASTER_SAT_THRESHOLD = 37,
    PARAM_MASTER_THRESH = 38,
    PARAM_MASTER_RATIO = 39,
    PARAM_MASTER_KNEE = 40,
    PARAM_MASTER_ATTACK = 41,
    PARAM_MASTER_RELEASE = 42,
    PARAM_MASTER_CEILING = 43,
  };

  class BandProcessor {
   public:
    void init(double sample_rate) {
      sample_rate_ = sample_rate;
      la_l_.resize(kMaxLookahead, 0.0f);
      la_r_.resize(kMaxLookahead, 0.0f);
      reset();
    }

    void reset() {
      envelope_db_ = 0.0f;
      limiter_envelope_ = 1.0f;
      std::fill(la_l_.begin(), la_l_.end(), 0.0f);
      std::fill(la_r_.begin(), la_r_.end(), 0.0f);
      la_write_ = 0;
      max_gr_db_ = 0.0f;
      peak_in_db_ = -200.0f;
      peak_out_db_ = -200.0f;
    }

    void setSampleRate(double sample_rate) { sample_rate_ = sample_rate; }

    float getGainReductionDb() const { return max_gr_db_; }
    float getInputDb() const { return peak_in_db_; }
    float getOutputDb() const { return peak_out_db_; }

    void process(float* in_l, float* in_r, float* out_l, float* out_r,
                 int num_samples, float pre_gain_db, float post_gain_db,
                 float sat_amount, float sat_threshold_db, float thresh_db,
                 float ratio, float knee_db, float attack_ms, float release_ms,
                 float ceiling_db, float lookahead_ms);

   private:
    double sample_rate_ = 44100.0;
    float envelope_db_ = 0.0f;
    float limiter_envelope_ = 1.0f;
    std::vector<float> la_l_, la_r_;
    int la_write_ = 0;
    float max_gr_db_ = 0.0f;
    float peak_in_db_ = -200.0f;
    float peak_out_db_ = -200.0f;

    static float saturate(float x, float threshold, float amount);
    static float computeGainReduction(float input_db, float threshold,
                                      float ratio, float knee_db);
  };

  BuiltinMaxim();

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

  // Parameter mappings
  static float normToFreq(double norm, float min_hz, float max_hz);
  static float normToGainDb(double norm);       // -24 to +24 dB
  static float normToSatThreshDb(double norm);  // -30 to 0 dB
  static float normToThreshDb(double norm);     // -60 to 0 dB
  static float normToRatio(double norm);        // 1 to 1000 (inf)
  static float normToKneeDb(double norm);       // 0 to 24 dB
  static float normToAttackMs(double norm);     // 0.1 to 100 ms
  static float normToReleaseMs(double norm);    // 10 to 1000 ms
  static float normToCeilingDb(double norm);    // -12 to 0 dB
  static float normToLookaheadMs(double norm);  // 0.1 to 10 ms

  // Metering getters
  float getBandGainReductionDb(
      int band) const;  // 0=Low, 1=Mid, 2=High, 3=Master
  float getBandInputDb(int band) const;
  float getBandOutputDb(int band) const;

 private:
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  // LR4 Crossover Filters (2 cascaded biquads per split per channel)
  BiquadFilter lp1_L_[2], lp1_R_[2];  // Low-pass for Low/Mid split
  BiquadFilter hp1_L_[2], hp1_R_[2];  // High-pass for Low/Mid split
  BiquadFilter lp2_L_[2],
      lp2_R_[2];  // Low-pass for Mid/High split (on hp1 output)
  BiquadFilter hp2_L_[2],
      hp2_R_[2];  // High-pass for Mid/High split (on hp1 output)
  BiquadFilter lp2_low_L_[2],
      lp2_low_R_[2];  // Low-pass for Low band phase-alignment
  BiquadFilter hp2_low_L_[2],
      hp2_low_R_[2];  // High-pass for Low band phase-alignment

  BandProcessor bands_[4];  // 0=Low, 1=Mid, 2=High, 3=Master

  int64_t last_time_samples_ = -1;  // For transport reset

  void reset();
  void updateCrossoverCoeffs();
};

}  // namespace hibiki
