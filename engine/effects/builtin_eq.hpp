#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/fft.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

struct EqDspState;

EqDspState* d_eq_create();
void d_eq_destroy(EqDspState* state);
void d_eq_reset(EqDspState* state);
void d_eq_set_parameter(EqDspState* state, uint32_t id, double value);
double d_eq_get_parameter(const EqDspState* state, uint32_t id);
bool d_eq_is_enabled(const EqDspState* state);
float d_eq_get_magnitude_db(const EqDspState* state, float freq);
void d_eq_process(EqDspState* state, float* out_l, float* out_r,
                  int num_samples, double sample_rate);
float d_eq_norm_to_freq(double norm);
double d_eq_freq_to_norm(float freq);
float d_eq_norm_to_q(double norm);
double d_eq_q_to_norm(float q);

// 8-band parametric EQ implemented as an IPlugin.
// Filter types: Off, LPF, HPF, Low Shelf, High Shelf, Bell (peaking).
// Uses Robert Bristow-Johnson's Audio EQ Cookbook biquad formulas.
class BuiltinEq : public IPlugin {
 public:
  static constexpr int kNumBands = 8;
  static constexpr int kParamsPerBand = 4;  // type, freq, gain, q
  static constexpr int kTotalParams =
      kNumBands * kParamsPerBand + 1;  // +1 for enable
  static constexpr int kSpectrumBins = 64;
  static constexpr const char* kPath = "builtin://eq";
  static constexpr const char* kName = "EQ Eight";

  BuiltinEq();
  ~BuiltinEq() override;

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

  // --- Frequency response calculation for UI ---
  float getMagnitudeDb(float freq) const;

  // --- Spectrum data for UI ---
  struct SpectrumData {
    float input_db[kSpectrumBins] = {};
    float output_db[kSpectrumBins] = {};
  };
  SpectrumData getSpectrumData() const;

  // --- Parameter mapping (public for tests) ---
  static float normToFreq(double norm);
  static double freqToNorm(float freq);
  static float normToQ(double norm);
  static double qToNorm(float q);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  void reset();

  // --- FFT-based spectrum analysis ---
  static constexpr int kFftSize = SpectrumAnalyzer::kFftSize;
  SpectrumAnalyzer spectrum_analyzer_;

  float input_ring_[kFftSize] = {};
  float output_ring_[kFftSize] = {};
  int spectrum_sample_count_ = 0;

  std::atomic<float> spectrum_input_db_[kSpectrumBins];
  std::atomic<float> spectrum_output_db_[kSpectrumBins];

  void initSpectrumBins();
  void maybeUpdateSpectrum();
};

}  // namespace hibiki
