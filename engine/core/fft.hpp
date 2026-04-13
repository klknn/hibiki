#pragma once

#include <atomic>
#include <cstddef>

namespace hibiki {

// FFT-based spectrum analyzer utility.
// Computes magnitude spectrum in dB from a time-domain ring buffer.
class SpectrumAnalyzer {
 public:
  static constexpr int kFftSize = 1024;
  static constexpr int kFftComplex = kFftSize / 2 + 1;

  // Initialize the analyzer for the given number of display bins.
  // Must be called before use.
  void init(int num_bins, float sample_rate);

  // Write mono samples into a ring buffer.
  void accumulateToRing(const float* L, const float* R, int n, float* ring);

  // Run real FFT on a ring buffer and write dB values into spectrum atomics.
  void computeSpectrum(const float* ring, std::atomic<float>* out_db);

  int ringPos() const { return ring_pos_; }
  void advanceRingPos(int n) { ring_pos_ = (ring_pos_ + n) & (kFftSize - 1); }

 private:
  int num_bins_ = 0;
  float sample_rate_ = 44100.0f;
  int ring_pos_ = 0;

  float hann_window_[kFftSize] = {};
  double fft_in_[kFftSize] = {};
  // fft_out_ stored in cpp to avoid exposing pocketfft/complex in header
  float bin_edges_[129] = {};  // max 128 bins + 1
};

}  // namespace hibiki
