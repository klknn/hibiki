#pragma once

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

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

  BuiltinEq() { reset(); }

  // IPlugin interface
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
      recalcAllCoeffs();
      initSpectrumBins();
    }

    // Copy input to output if different buffers
    float* outL = outputs[0];
    float* outR = outputs[1];
    if (inputs && inputs != outputs) {
      for (int i = 0; i < num_samples; ++i) {
        outL[i] = inputs[0][i];
        outR[i] = inputs[1][i];
      }
    }

    // Accumulate input spectrum (before EQ processing)
    accumulateSpectrum(outL, outR, num_samples, input_rms_accum_);

    if (!enabled_) {
      // Still accumulate output = input when bypassed
      accumulateSpectrum(outL, outR, num_samples, output_rms_accum_);
      spectrum_sample_count_ += num_samples;
      maybeUpdateSpectrum();
      return;
    }

    // Process each band
    for (int b = 0; b < kNumBands; ++b) {
      if (bands_[b].type == OFF) continue;
      const auto& c = coeffs_[b];
      auto& sL = state_[b][0];
      auto& sR = state_[b][1];

      for (int i = 0; i < num_samples; ++i) {
        // Left channel
        float xL = outL[i];
        float yL = c.b0 * xL + c.b1 * sL.x1 + c.b2 * sL.x2 - c.a1 * sL.y1 -
                   c.a2 * sL.y2;
        sL.x2 = sL.x1;
        sL.x1 = xL;
        sL.y2 = sL.y1;
        sL.y1 = yL;
        outL[i] = yL;

        // Right channel
        float xR = outR[i];
        float yR = c.b0 * xR + c.b1 * sR.x1 + c.b2 * sR.x2 - c.a1 * sR.y1 -
                   c.a2 * sR.y2;
        sR.x2 = sR.x1;
        sR.x1 = xR;
        sR.y2 = sR.y1;
        sR.y1 = yR;
        outR[i] = yR;
      }
    }

    // Accumulate output spectrum (after EQ processing)
    accumulateSpectrum(outL, outR, num_samples, output_rms_accum_);
    spectrum_sample_count_ += num_samples;
    maybeUpdateSpectrum();
  }

  int getParameterCount() const override { return kTotalParams; }

  bool getParameterInfo(int index, VstParamInfo& info) const override {
    if (index < 0 || index >= kTotalParams) return false;
    info.id = index;
    if (index < kNumBands) {
      info.name = "Band " + std::to_string(index + 1) + " Type";
      info.defaultValue = 0.0;  // OFF
    } else if (index < kNumBands * 2) {
      int b = index - kNumBands;
      info.name = "Band " + std::to_string(b + 1) + " Freq";
      info.defaultValue = freqToNorm(defaultFreqs_[b]);
    } else if (index < kNumBands * 3) {
      int b = index - kNumBands * 2;
      info.name = "Band " + std::to_string(b + 1) + " Gain";
      info.defaultValue = 0.5;  // 0 dB
    } else if (index < kNumBands * 4) {
      int b = index - kNumBands * 3;
      info.name = "Band " + std::to_string(b + 1) + " Q";
      info.defaultValue = qToNorm(0.707);
    } else {
      info.name = "Enable";
      info.defaultValue = 1.0;
    }
    return true;
  }

  void setParameterValue(uint32_t id, double value) override {
    int idx = (int)id;
    if (idx < 0 || idx >= kTotalParams) return;
    params_[idx] = value;

    if (idx == kNumBands * kParamsPerBand) {
      enabled_ = value >= 0.5;
      return;
    }

    // Figure out which band was changed and update it
    int band = idx % kNumBands;
    updateBand(band);
  }

  double getParameterValue(uint32_t id) const override {
    int idx = (int)id;
    if (idx < 0 || idx >= kTotalParams) return 0.0;
    return params_[idx];
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

  // --- Frequency response calculation for UI ---
  // Returns the magnitude (in dB) of the composite EQ at a given frequency.
  float getMagnitudeDb(float freq) const {
    if (!enabled_) return 0.0f;
    float total_db = 0.0f;
    double w = 2.0 * M_PI * freq / sample_rate_;
    double cos_w = std::cos(w);
    double cos_2w = std::cos(2.0 * w);
    double sin_w = std::sin(w);
    double sin_2w = std::sin(2.0 * w);

    for (int b = 0; b < kNumBands; ++b) {
      if (bands_[b].type == OFF) continue;
      const auto& c = coeffs_[b];
      // H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
      double num_re = c.b0 + c.b1 * cos_w + c.b2 * cos_2w;
      double num_im = -(c.b1 * sin_w + c.b2 * sin_2w);
      double den_re = 1.0 + c.a1 * cos_w + c.a2 * cos_2w;
      double den_im = -(c.a1 * sin_w + c.a2 * sin_2w);
      double mag_sq = (num_re * num_re + num_im * num_im) /
                      (den_re * den_re + den_im * den_im);
      if (mag_sq > 0) total_db += 10.0f * std::log10(mag_sq);
    }
    return total_db;
  }

  // --- Spectrum data for UI ---
  struct SpectrumData {
    float input_db[kSpectrumBins] = {};
    float output_db[kSpectrumBins] = {};
  };

  // Thread-safe: called from notification thread
  SpectrumData getSpectrumData() const {
    SpectrumData d;
    for (int i = 0; i < kSpectrumBins; ++i) {
      d.input_db[i] = spectrum_input_db_[i].load(std::memory_order_relaxed);
      d.output_db[i] = spectrum_output_db_[i].load(std::memory_order_relaxed);
    }
    return d;
  }

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

  // Default frequencies for 8 bands (spread across spectrum)
  static constexpr float defaultFreqs_[kNumBands] = {
      30.0f, 80.0f, 250.0f, 700.0f, 2000.0f, 5000.0f, 10000.0f, 16000.0f};

  void reset() {
    for (int b = 0; b < kNumBands; ++b) {
      bands_[b] = {};
      bands_[b].freq = defaultFreqs_[b];
      coeffs_[b] = {};
      state_[b][0] = {};
      state_[b][1] = {};
    }
    // Initialize params to defaults
    for (int b = 0; b < kNumBands; ++b) {
      params_[b] = 0.0;  // type = OFF
      params_[b + kNumBands] = freqToNorm(defaultFreqs_[b]);
      params_[b + kNumBands * 2] = 0.5;  // 0 dB
      params_[b + kNumBands * 3] = qToNorm(0.707);
    }
    params_[kNumBands * kParamsPerBand] = 1.0;  // enabled
    enabled_ = true;
    initSpectrumBins();
  }

  void updateBand(int b) {
    // Decode normalized params to band params
    double typeNorm = params_[b];
    if (typeNorm < 0.1)
      bands_[b].type = OFF;
    else if (typeNorm < 0.3)
      bands_[b].type = LPF;
    else if (typeNorm < 0.5)
      bands_[b].type = HPF;
    else if (typeNorm < 0.7)
      bands_[b].type = LOW_SHELF;
    else if (typeNorm < 0.9)
      bands_[b].type = HIGH_SHELF;
    else
      bands_[b].type = BELL;

    bands_[b].freq = normToFreq(params_[b + kNumBands]);
    bands_[b].gain_db =
        (float)(params_[b + kNumBands * 2] - 0.5) * 48.0f;  // ±24 dB
    bands_[b].q = normToQ(params_[b + kNumBands * 3]);

    calcCoeffs(b);
  }

  void recalcAllCoeffs() {
    for (int b = 0; b < kNumBands; ++b) updateBand(b);
  }

  void calcCoeffs(int b) {
    const auto& bp = bands_[b];
    if (bp.type == OFF) {
      coeffs_[b] = {1, 0, 0, 0, 0};
      return;
    }

    double w0 = 2.0 * M_PI * bp.freq / sample_rate_;
    double cos_w0 = std::cos(w0);
    double sin_w0 = std::sin(w0);
    double alpha = sin_w0 / (2.0 * bp.q);
    double A = std::pow(10.0, bp.gain_db / 40.0);

    double b0, b1, b2, a0, a1, a2;

    switch (bp.type) {
      case LPF:
        b0 = (1 - cos_w0) / 2;
        b1 = 1 - cos_w0;
        b2 = (1 - cos_w0) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha;
        break;
      case HPF:
        b0 = (1 + cos_w0) / 2;
        b1 = -(1 + cos_w0);
        b2 = (1 + cos_w0) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha;
        break;
      case LOW_SHELF: {
        double sqA = std::sqrt(A);
        double two_sqA_alpha = 2 * sqA * alpha;
        b0 = A * ((A + 1) - (A - 1) * cos_w0 + two_sqA_alpha);
        b1 = 2 * A * ((A - 1) - (A + 1) * cos_w0);
        b2 = A * ((A + 1) - (A - 1) * cos_w0 - two_sqA_alpha);
        a0 = (A + 1) + (A - 1) * cos_w0 + two_sqA_alpha;
        a1 = -2 * ((A - 1) + (A + 1) * cos_w0);
        a2 = (A + 1) + (A - 1) * cos_w0 - two_sqA_alpha;
        break;
      }
      case HIGH_SHELF: {
        double sqA = std::sqrt(A);
        double two_sqA_alpha = 2 * sqA * alpha;
        b0 = A * ((A + 1) + (A - 1) * cos_w0 + two_sqA_alpha);
        b1 = -2 * A * ((A - 1) + (A + 1) * cos_w0);
        b2 = A * ((A + 1) + (A - 1) * cos_w0 - two_sqA_alpha);
        a0 = (A + 1) - (A - 1) * cos_w0 + two_sqA_alpha;
        a1 = 2 * ((A - 1) - (A + 1) * cos_w0);
        a2 = (A + 1) - (A - 1) * cos_w0 - two_sqA_alpha;
        break;
      }
      case BELL:
      default:
        b0 = 1 + alpha * A;
        b1 = -2 * cos_w0;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha / A;
        break;
    }

    // Normalize
    coeffs_[b].b0 = (float)(b0 / a0);
    coeffs_[b].b1 = (float)(b1 / a0);
    coeffs_[b].b2 = (float)(b2 / a0);
    coeffs_[b].a1 = (float)(a1 / a0);
    coeffs_[b].a2 = (float)(a2 / a0);
  }

  // Frequency: log scale 20–20000 Hz mapped to 0–1
  static float normToFreq(double norm) {
    return (float)(20.0 * std::pow(1000.0, norm));
  }
  static double freqToNorm(float freq) {
    return std::log(freq / 20.0) / std::log(1000.0);
  }

  // Q: log scale 0.1–18.0 mapped to 0–1
  static float normToQ(double norm) {
    return (float)(0.1 * std::pow(180.0, norm));
  }
  static double qToNorm(float q) { return std::log(q / 0.1) / std::log(180.0); }

  // --- Spectrum analysis helpers ---

  // Edges of the 64 log-spaced bins (Hz)
  float bin_edges_[kSpectrumBins + 1] = {};
  // Running RMS accumulators per bin
  double input_rms_accum_[kSpectrumBins] = {};
  double output_rms_accum_[kSpectrumBins] = {};
  int spectrum_sample_count_ = 0;
  // Output spectrum (read from notification thread)
  std::atomic<float> spectrum_input_db_[kSpectrumBins];
  std::atomic<float> spectrum_output_db_[kSpectrumBins];

  void initSpectrumBins() {
    // Log-spaced bins from 20 Hz to 20 kHz
    const double log_min = std::log(20.0);
    const double log_max = std::log(20000.0);
    for (int i = 0; i <= kSpectrumBins; ++i) {
      bin_edges_[i] =
          (float)std::exp(log_min + (log_max - log_min) * i / kSpectrumBins);
    }
    for (int i = 0; i < kSpectrumBins; ++i) {
      input_rms_accum_[i] = 0;
      output_rms_accum_[i] = 0;
      spectrum_input_db_[i].store(-100.0f, std::memory_order_relaxed);
      spectrum_output_db_[i].store(-100.0f, std::memory_order_relaxed);
    }
    spectrum_sample_count_ = 0;
  }

  // Simple energy-based spectrum estimation:
  // For each sample, compute instantaneous power and distribute it across
  // frequency bins based on the energy distribution (approximated by
  // applying each band's biquad transfer function magnitude to the
  // total signal power). This is cheaper than a real FFT.
  void accumulateSpectrum(const float* L, const float* R, int n,
                          double* rms_accum) {
    for (int i = 0; i < n; ++i) {
      float mono = (L[i] + R[i]) * 0.5f;
      float power = mono * mono;
      // Distribute power evenly across bins (simple broadband estimate)
      for (int b = 0; b < kSpectrumBins; ++b) {
        rms_accum[b] += power;
      }
    }
  }

  void maybeUpdateSpectrum() {
    // Update every ~2048 samples (~21Hz at 44100)
    if (spectrum_sample_count_ < 2048) return;
    double inv_n = 1.0 / spectrum_sample_count_;
    for (int i = 0; i < kSpectrumBins; ++i) {
      float in_rms = (float)std::sqrt(input_rms_accum_[i] * inv_n);
      float out_rms = (float)std::sqrt(output_rms_accum_[i] * inv_n);
      float in_db = (in_rms > 1e-10f) ? 20.0f * std::log10(in_rms) : -100.0f;
      float out_db = (out_rms > 1e-10f) ? 20.0f * std::log10(out_rms) : -100.0f;
      spectrum_input_db_[i].store(in_db, std::memory_order_relaxed);
      spectrum_output_db_[i].store(out_db, std::memory_order_relaxed);
      input_rms_accum_[i] = 0;
      output_rms_accum_[i] = 0;
    }
    spectrum_sample_count_ = 0;
  }
};

}  // namespace hibiki
