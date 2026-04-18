#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/core/fft.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

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

  enum FilterType { OFF = 0, LPF, HPF, LOW_SHELF, HIGH_SHELF, BELL };

  BuiltinEq();

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
  struct BandParams {
    FilterType type = OFF;
    float freq = 1000.0f;
    float gain_db = 0.0f;
    float q = 0.707f;
  };

  struct BiquadCoeffs {
    float b0 = 1, b1 = 0, b2 = 0;
    float a1 = 0, a2 = 0;
  };

  struct BiquadState {
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  };

  BandParams bands_[kNumBands];
  BiquadCoeffs coeffs_[kNumBands];
  BiquadState state_[kNumBands][2];  // [band][channel]
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  static constexpr float defaultFreqs_[kNumBands] = {
      30.0f, 80.0f, 250.0f, 700.0f, 2000.0f, 5000.0f, 10000.0f, 16000.0f};

  void reset();
  void updateBand(int b);
  void recalcAllCoeffs();
  void calcCoeffs(int b);

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
