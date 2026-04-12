#pragma once

#include <atomic>
#include <cmath>
#include <complex>
#include <string>
#include <numbers>
#include <vector>

#include "engine/plugin/iplugin.hpp"
#include "pocketfft_hdronly.h"

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
    accumulateToRing(outL, outR, num_samples, input_ring_);

    if (!enabled_) {
      // Still accumulate output = input when bypassed
      accumulateToRing(outL, outR, num_samples, output_ring_);
      ring_pos_ = (ring_pos_ + num_samples) & (kFftSize - 1);
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
    accumulateToRing(outL, outR, num_samples, output_ring_);
    ring_pos_ = (ring_pos_ + num_samples) & (kFftSize - 1);
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
    double w = 2.0 * std::numbers::pi_v<double> * freq / sample_rate_;
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

    double w0 = 2.0 * std::numbers::pi_v<double> * bp.freq / sample_rate_;
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

  // --- FFT-based spectrum analysis ---
  static constexpr int kFftSize = 1024;
  static constexpr int kFftComplex = kFftSize / 2 + 1;  // 513 bins

  // Circular buffers for input and output mono samples
  float input_ring_[kFftSize] = {};
  float output_ring_[kFftSize] = {};
  int ring_pos_ = 0;
  int spectrum_sample_count_ = 0;

  // Pre-computed Hann window
  float hann_window_[kFftSize] = {};

  // Pre-allocated FFT work buffers (avoid audio-thread allocations)
  double fft_in_[kFftSize] = {};
  std::complex<double> fft_out_[kFftComplex] = {};

  // Log-spaced bin edges for mapping FFT bins → display bins
  float bin_edges_[kSpectrumBins + 1] = {};

  // Output spectrum (read from notification thread)
  std::atomic<float> spectrum_input_db_[kSpectrumBins];
  std::atomic<float> spectrum_output_db_[kSpectrumBins];

  void initSpectrumBins() {
    // Pre-compute Hann window
    for (int i = 0; i < kFftSize; ++i) {
      hann_window_[i] =
          0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (kFftSize - 1)));
    }
    // Log-spaced bin edges from 20 Hz to 20 kHz
    const double log_min = std::log(20.0);
    const double log_max = std::log(20000.0);
    for (int i = 0; i <= kSpectrumBins; ++i) {
      bin_edges_[i] =
          (float)std::exp(log_min + (log_max - log_min) * i / kSpectrumBins);
    }
    for (int i = 0; i < kSpectrumBins; ++i) {
      spectrum_input_db_[i].store(-100.0f, std::memory_order_relaxed);
      spectrum_output_db_[i].store(-100.0f, std::memory_order_relaxed);
    }
    ring_pos_ = 0;
    spectrum_sample_count_ = 0;
  }

  // Write mono samples into a ring buffer without advancing ring_pos_
  void accumulateToRing(const float* L, const float* R, int n, float* ring) {
    int pos = ring_pos_;
    for (int i = 0; i < n; ++i) {
      ring[pos] = (L[i] + R[i]) * 0.5f;
      pos = (pos + 1) & (kFftSize - 1);
    }
  }

  // Run real FFT on a ring buffer and write dB values into spectrum atomics
  void computeSpectrum(const float* ring, std::atomic<float>* out_db) {
    // Copy ring buffer with Hann window into fft_in_, unwrapping from ring_pos_
    for (int i = 0; i < kFftSize; ++i) {
      int idx = (ring_pos_ + i) & (kFftSize - 1);
      fft_in_[i] = (double)ring[idx] * hann_window_[i];
    }

    // Run pocketfft real-to-complex FFT
    pocketfft::shape_t shape = {(size_t)kFftSize};
    pocketfft::stride_t stride_in = {(ptrdiff_t)sizeof(double)};
    pocketfft::stride_t stride_out = {(ptrdiff_t)sizeof(std::complex<double>)};
    pocketfft::r2c(shape, stride_in, stride_out, /*axes=*/{0},
                   pocketfft::FORWARD, fft_in_, fft_out_, 1.0);

    // Map FFT bins to log-spaced display bins
    double freq_per_bin = sample_rate_ / kFftSize;
    for (int b = 0; b < kSpectrumBins; ++b) {
      float f_lo = bin_edges_[b];
      float f_hi = bin_edges_[b + 1];
      int k_lo = std::max(1, (int)(f_lo / freq_per_bin));
      int k_hi = std::min(kFftComplex - 1, (int)(f_hi / freq_per_bin) + 1);

      double sum_mag_sq = 0;
      int count = 0;
      for (int k = k_lo; k < k_hi; ++k) {
        double re = fft_out_[k].real();
        double im = fft_out_[k].imag();
        sum_mag_sq += re * re + im * im;
        count++;
      }

      float db = -100.0f;
      if (count > 0) {
        // Normalize: 2/N for one-sided spectrum, average over bins
        double mag = std::sqrt(sum_mag_sq / count) * (2.0 / kFftSize);
        if (mag > 1e-10) db = 20.0f * (float)std::log10(mag);
      }
      out_db[b].store(db, std::memory_order_relaxed);
    }
  }

  void maybeUpdateSpectrum() {
    // Update every kFftSize samples (~23 Hz at 44100)
    if (spectrum_sample_count_ < kFftSize) return;
    computeSpectrum(input_ring_, spectrum_input_db_);
    computeSpectrum(output_ring_, spectrum_output_db_);
    spectrum_sample_count_ = 0;
  }
};

}  // namespace hibiki
