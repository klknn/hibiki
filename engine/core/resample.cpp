#include "engine/core/resample.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hibiki {

extern void d_rational_approx(double ratio, int max_den, int* out_a,
                              int* out_b);
extern void d_build_polyphase_bank(int factor_b, int filter_radius,
                                   double cutoff, double kaiser_beta,
                                   float* out_bank);
extern void d_resample_process(const float* input, int num_input_frames,
                               int num_channels, int factor_a, int factor_b,
                               int filter_radius, const float* flat_bank,
                               float* output, size_t num_output_frames);

Resampler::Resampler(float input_rate, float output_rate, int num_channels,
                     float filter_radius_factor, float kaiser_beta,
                     float cutoff_proportion)
    : num_channels_(num_channels) {
  double ratio = static_cast<double>(input_rate) / output_rate;
  d_rational_approx(ratio, 1000, &factor_a_, &factor_b_);

  double max_ratio = std::max(1.0, ratio);
  filter_radius_ =
      static_cast<int>(std::ceil(filter_radius_factor * max_ratio));
  double cutoff = cutoff_proportion / (2.0 * max_ratio);

  int filter_len = 2 * filter_radius_;
  flat_filter_bank_.resize(static_cast<size_t>(factor_b_ * filter_len));
  d_build_polyphase_bank(factor_b_, filter_radius_, cutoff, kaiser_beta,
                         flat_filter_bank_.data());
}

std::vector<float> Resampler::Process(const float* input,
                                      int num_input_frames) const {
  if (num_input_frames <= 0 || num_channels_ <= 0) return {};

  size_t num_output_frames = static_cast<size_t>(
      std::ceil(static_cast<double>(num_input_frames) * factor_b_ / factor_a_));
  std::vector<float> output(num_output_frames *
                            static_cast<size_t>(num_channels_));

  d_resample_process(input, num_input_frames, num_channels_, factor_a_,
                     factor_b_, filter_radius_, flat_filter_bank_.data(),
                     output.data(), num_output_frames);
  return output;
}

}  // namespace hibiki
