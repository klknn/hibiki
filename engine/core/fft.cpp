#include "engine/core/fft.hpp"

#include <cmath>
#include <complex>
#include <numbers>

#include "pocketfft_hdronly.h"

namespace hibiki {

namespace {
std::complex<double> g_fft_out[SpectrumAnalyzer::kFftComplex];
}

void SpectrumAnalyzer::init(int num_bins, float sample_rate) {
  num_bins_ = num_bins;
  sample_rate_ = sample_rate;
  ring_pos_ = 0;

  // Pre-compute Hann window
  for (int i = 0; i < kFftSize; ++i) {
    hann_window_[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> *
                                              i / (kFftSize - 1)));
  }

  // Log-spaced bin edges from 20 Hz to 20 kHz
  const double log_min = std::log(20.0);
  const double log_max = std::log(20000.0);
  for (int i = 0; i <= num_bins_; ++i) {
    bin_edges_[i] =
        (float)std::exp(log_min + (log_max - log_min) * i / num_bins_);
  }
}

void SpectrumAnalyzer::accumulateToRing(const float* L, const float* R, int n,
                                        float* ring) {
  int pos = ring_pos_;
  for (int i = 0; i < n; ++i) {
    ring[pos] = (L[i] + R[i]) * 0.5f;
    pos = (pos + 1) & (kFftSize - 1);
  }
}

void SpectrumAnalyzer::computeSpectrum(const float* ring,
                                       std::atomic<float>* out_db) {
  // Copy ring buffer with Hann window into fft_in_, unwrapping from ring_pos_
  for (int i = 0; i < kFftSize; ++i) {
    int idx = (ring_pos_ + i) & (kFftSize - 1);
    fft_in_[i] = (double)ring[idx] * hann_window_[i];
  }

  // Run pocketfft real-to-complex FFT
  pocketfft::shape_t shape = {(size_t)kFftSize};
  pocketfft::stride_t stride_in = {(ptrdiff_t)sizeof(double)};
  pocketfft::stride_t stride_out = {(ptrdiff_t)sizeof(std::complex<double>)};
  pocketfft::r2c(shape, stride_in, stride_out, /*axes=*/{0}, pocketfft::FORWARD,
                 fft_in_, g_fft_out, 1.0);

  // Map FFT bins to log-spaced display bins
  double freq_per_bin = sample_rate_ / kFftSize;
  for (int b = 0; b < num_bins_; ++b) {
    float f_lo = bin_edges_[b];
    float f_hi = bin_edges_[b + 1];
    int k_lo = std::max(1, (int)(f_lo / freq_per_bin));
    int k_hi = std::min(kFftComplex - 1, (int)(f_hi / freq_per_bin) + 1);

    double sum_mag_sq = 0;
    int count = 0;
    for (int k = k_lo; k < k_hi; ++k) {
      double re = g_fft_out[k].real();
      double im = g_fft_out[k].imag();
      sum_mag_sq += re * re + im * im;
      count++;
    }

    float db = -100.0f;
    if (count > 0) {
      double mag = std::sqrt(sum_mag_sq / count) * (2.0 / kFftSize);
      if (mag > 1e-10) db = 20.0f * (float)std::log10(mag);
    }
    out_db[b].store(db, std::memory_order_relaxed);
  }
}

}  // namespace hibiki
