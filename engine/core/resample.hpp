#pragma once

#include <vector>

namespace hibiki {

// Kaiser-windowed sinc polyphase FIR resampler (backed by @nogc D kernel).
class Resampler {
 public:
  Resampler(float input_rate, float output_rate, int num_channels,
            float filter_radius_factor = 5.0f, float kaiser_beta = 5.658f,
            float cutoff_proportion = 0.9f);

  std::vector<float> Process(const float* input, int num_input_frames) const;

  std::vector<float> Process(const std::vector<float>& input,
                             int num_input_frames) const {
    return Process(input.data(), num_input_frames);
  }

  int num_channels() const { return num_channels_; }
  int factor_numerator() const { return factor_a_; }
  int factor_denominator() const { return factor_b_; }

 private:
  int num_channels_;
  int factor_a_;
  int factor_b_;
  int filter_radius_;
  std::vector<float> flat_filter_bank_;
};

}  // namespace hibiki
