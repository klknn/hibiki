#pragma once

#include <vector>

namespace hibiki {

// Kaiser-windowed sinc polyphase FIR resampler.
// Converts audio from one sample rate to another using rational approximation
// of the rate ratio and precomputed polyphase filter banks.
class Resampler {
 public:
  // Create a resampler for the given rate conversion and channel count.
  // filter_radius_factor controls quality (5.0 = good, 17.0 = very high).
  Resampler(float input_rate, float output_rate, int num_channels,
            float filter_radius_factor = 5.0f, float kaiser_beta = 5.658f,
            float cutoff_proportion = 0.9f);

  // Resample an entire buffer. Input is interleaved float samples.
  // Returns interleaved float output.
  std::vector<float> Process(const float* input, int num_input_frames) const;

  // Convenience overload.
  std::vector<float> Process(const std::vector<float>& input,
                             int num_input_frames) const {
    return Process(input.data(), num_input_frames);
  }

  int num_channels() const { return num_channels_; }
  int factor_numerator() const { return factor_a_; }
  int factor_denominator() const { return factor_b_; }

 private:
  int num_channels_;
  int factor_a_;       // numerator: input_rate / output_rate ≈ a/b
  int factor_b_;       // denominator (number of polyphase filter phases)
  int filter_radius_;  // half-length of each phase filter in input samples

  // Polyphase filter bank: filter_bank_[phase][tap]
  // Dimensions: factor_b_ phases × (2 * filter_radius_) taps.
  std::vector<std::vector<float>> filter_bank_;
};

}  // namespace hibiki
