#include "engine/core/resample.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace hibiki {
namespace {

// Modified Bessel function of the first kind, order 0 (I0).
// Used for the Kaiser window. Computed via power series.
double BesselI0(double x) {
  double sum = 1.0;
  double term = 1.0;
  double half_x = x / 2.0;
  for (int k = 1; k < 30; ++k) {
    term *= (half_x / k) * (half_x / k);
    sum += term;
    if (term < sum * 1e-15) break;
  }
  return sum;
}

// Kaiser window: w(n) = I0(beta * sqrt(1 - (2n/N - 1)^2)) / I0(beta)
// where n is in [0, N]. Here we parameterize by continuous t in [-1, 1].
double KaiserWindow(double t, double beta) {
  if (t <= -1.0 || t >= 1.0) return 0.0;
  return BesselI0(beta * std::sqrt(1.0 - t * t)) / BesselI0(beta);
}

// Normalized sinc: sin(pi*x) / (pi*x), with sinc(0) = 1.
double Sinc(double x) {
  if (std::abs(x) < 1e-12) return 1.0;
  double px = std::numbers::pi_v<double> * x;
  return std::sin(px) / px;
}

// Find the best rational approximation a/b ≈ ratio with b <= max_den.
// Uses the Stern-Brocot / mediant-based continued fraction algorithm.
void RationalApprox(double ratio, int max_den, int& out_a, int& out_b) {
  // Handle exact integers.
  if (ratio <= 0) {
    out_a = 0;
    out_b = 1;
    return;
  }

  int best_a = static_cast<int>(std::round(ratio));
  int best_b = 1;
  double best_err = std::abs(ratio - best_a);

  // Continued fraction convergents.
  int p0 = 0, q0 = 1;
  int p1 = 1, q1 = 0;
  double r = ratio;

  for (int i = 0; i < 50 && best_err > 1e-12; ++i) {
    int a_i = static_cast<int>(std::floor(r));
    int p2 = a_i * p1 + p0;
    int q2 = a_i * q1 + q0;
    if (q2 > max_den) {
      // Try the largest multiple that fits.
      int k = (max_den - q0) / q1;
      if (k > 0) {
        int pk = k * p1 + p0;
        int qk = k * q1 + q0;
        double err = std::abs(ratio - static_cast<double>(pk) / qk);
        if (err < best_err) {
          best_a = pk;
          best_b = qk;
          best_err = err;
        }
      }
      break;
    }
    double err = std::abs(ratio - static_cast<double>(p2) / q2);
    if (err < best_err) {
      best_a = p2;
      best_b = q2;
      best_err = err;
    }
    if (std::abs(r - a_i) < 1e-12) break;
    r = 1.0 / (r - a_i);
    p0 = p1;
    q0 = q1;
    p1 = p2;
    q1 = q2;
  }
  out_a = best_a;
  out_b = best_b;
}

}  // namespace

Resampler::Resampler(float input_rate, float output_rate, int num_channels,
                     float filter_radius_factor, float kaiser_beta,
                     float cutoff_proportion)
    : num_channels_(num_channels) {
  // Rational approximation: input_rate / output_rate ≈ a/b
  double ratio = static_cast<double>(input_rate) / output_rate;
  RationalApprox(ratio, 1000, factor_a_, factor_b_);

  // Determine filter radius in input samples.
  // If downsampling (ratio > 1), scale radius by ratio for anti-aliasing.
  double max_ratio = std::max(1.0, ratio);
  filter_radius_ =
      static_cast<int>(std::ceil(filter_radius_factor * max_ratio));

  // Cutoff frequency normalized to input sample rate.
  // For downsampling: cutoff at output Nyquist * cutoff_proportion.
  // For upsampling: cutoff at input Nyquist * cutoff_proportion.
  double cutoff = cutoff_proportion / (2.0 * max_ratio);

  int filter_len = 2 * filter_radius_;  // number of filter taps per phase

  // Build polyphase filter bank: b phases.
  filter_bank_.resize(factor_b_);
  for (int p = 0; p < factor_b_; ++p) {
    filter_bank_[p].resize(filter_len);
    double phase_offset = static_cast<double>(p) / factor_b_;
    double energy = 0.0;
    for (int k = 0; k < filter_len; ++k) {
      // Filter position relative to center.
      double t = (k - filter_radius_) + phase_offset;
      // Kaiser-windowed sinc.
      double window = KaiserWindow(t / filter_radius_, kaiser_beta);
      double h = 2.0 * cutoff * Sinc(2.0 * cutoff * t) * window;
      filter_bank_[p][k] = static_cast<float>(h);
      energy += h;
    }
    // Normalize to unity gain.
    if (std::abs(energy) > 1e-10) {
      for (int k = 0; k < filter_len; ++k) {
        filter_bank_[p][k] /= static_cast<float>(energy);
      }
    }
  }
}

std::vector<float> Resampler::Process(const float* input,
                                      int num_input_frames) const {
  if (num_input_frames <= 0 || num_channels_ <= 0) return {};

  // Estimate output size.
  int64_t num_output_frames = static_cast<int64_t>(
      std::ceil(static_cast<double>(num_input_frames) * factor_b_ / factor_a_));
  std::vector<float> output;
  output.reserve(num_output_frames * num_channels_);

  int filter_len = 2 * filter_radius_;

  // For each output sample m, compute:
  //   position = m * a / b  (in input sample coordinates)
  //   phase p = (m * a) mod b
  //   integer position q = (m * a) / b
  //   output[m] = sum_k filter_bank_[p][k] * input[q - filter_radius_ + k]

  int64_t accum = 0;  // = m * factor_a_, tracks position without rounding error
  for (int64_t m = 0; m < num_output_frames; ++m) {
    int p = static_cast<int>(accum % factor_b_);
    int q = static_cast<int>(accum / factor_b_);

    const float* phase_filter = filter_bank_[p].data();

    for (int ch = 0; ch < num_channels_; ++ch) {
      double sum = 0.0;
      for (int k = 0; k < filter_len; ++k) {
        int src_idx = q - filter_radius_ + k;
        if (src_idx >= 0 && src_idx < num_input_frames) {
          sum += phase_filter[k] * input[src_idx * num_channels_ + ch];
        }
        // Out-of-range samples treated as zero (zero-padding).
      }
      output.push_back(static_cast<float>(sum));
    }

    accum += factor_a_;
  }

  return output;
}

}  // namespace hibiki
