#include "engine/core/resample.hpp"

#include <cmath>
#include <numbers>
#include <vector>

#include "gtest/gtest.h"

namespace hibiki {
namespace {

TEST(ResamplerTest, IdentityRatio) {
  // 44100 → 44100: output should be same length and same values.
  Resampler r(44100, 44100, 1);
  EXPECT_EQ(r.factor_numerator(), 1);
  EXPECT_EQ(r.factor_denominator(), 1);

  constexpr int N = 1000;
  std::vector<float> input(N, 0.5f);
  auto output = r.Process(input, N);

  // Output should be approximately same length.
  EXPECT_NEAR(static_cast<int>(output.size()), N, 2);

  // DC value should be preserved.
  for (size_t i = 10; i < output.size() - 10; ++i) {
    EXPECT_NEAR(output[i], 0.5f, 0.01f) << "at sample " << i;
  }
}

TEST(ResamplerTest, Downsample48kTo16k) {
  // Factor 3:1 — output should be 1/3 the length.
  Resampler r(48000, 16000, 1);
  EXPECT_EQ(r.factor_numerator(), 3);
  EXPECT_EQ(r.factor_denominator(), 1);

  constexpr int N = 3000;
  std::vector<float> input(N, 1.0f);
  auto output = r.Process(input, N);

  // Expected ~1000 output frames.
  EXPECT_NEAR(static_cast<int>(output.size()), 1000, 5);

  // DC value should be preserved (unity gain).
  int stable_start = 20;
  int stable_end = static_cast<int>(output.size()) - 20;
  for (int i = stable_start; i < stable_end; ++i) {
    EXPECT_NEAR(output[i], 1.0f, 0.02f) << "at sample " << i;
  }
}

TEST(ResamplerTest, Upsample16kTo48k) {
  // Factor 1:3 — output should be 3x the length.
  Resampler r(16000, 48000, 1);
  EXPECT_EQ(r.factor_numerator(), 1);
  EXPECT_EQ(r.factor_denominator(), 3);

  constexpr int N = 1000;
  std::vector<float> input(N, 0.75f);
  auto output = r.Process(input, N);

  EXPECT_NEAR(static_cast<int>(output.size()), 3000, 5);

  // DC value preserved.
  int stable_start = 30;
  int stable_end = static_cast<int>(output.size()) - 30;
  for (int i = stable_start; i < stable_end; ++i) {
    EXPECT_NEAR(output[i], 0.75f, 0.02f) << "at sample " << i;
  }
}

TEST(ResamplerTest, Resample44100To48000) {
  // Non-trivial ratio 44100/48000 = 147/160.
  Resampler r(44100, 48000, 1);

  constexpr int N = 4410;
  std::vector<float> input(N, 0.6f);
  auto output = r.Process(input, N);

  // Expected ~4800 output frames.
  int expected =
      static_cast<int>(std::ceil(static_cast<double>(N) * 48000.0 / 44100.0));
  EXPECT_NEAR(static_cast<int>(output.size()), expected, 3);

  // DC value preserved.
  int stable_start = 30;
  int stable_end = static_cast<int>(output.size()) - 30;
  for (int i = stable_start; i < stable_end; ++i) {
    EXPECT_NEAR(output[i], 0.6f, 0.02f) << "at sample " << i;
  }
}

TEST(ResamplerTest, Stereo) {
  // Stereo DC test.
  Resampler r(48000, 16000, 2);

  constexpr int N = 3000;
  std::vector<float> input(N * 2);
  for (int i = 0; i < N; ++i) {
    input[2 * i] = 0.3f;      // left
    input[2 * i + 1] = 0.7f;  // right
  }
  auto output = r.Process(input, N);

  int num_out_frames = static_cast<int>(output.size()) / 2;
  EXPECT_NEAR(num_out_frames, 1000, 5);

  int stable_start = 20;
  int stable_end = num_out_frames - 20;
  for (int i = stable_start; i < stable_end; ++i) {
    EXPECT_NEAR(output[2 * i], 0.3f, 0.02f) << "L at frame " << i;
    EXPECT_NEAR(output[2 * i + 1], 0.7f, 0.02f) << "R at frame " << i;
  }
}

TEST(ResamplerTest, SineWaveEnergy) {
  // Verify that a 1kHz sine at 44.1kHz resampled to 48kHz preserves energy.
  Resampler r(44100, 48000, 1);

  constexpr int N = 44100;  // 1 second
  std::vector<float> input(N);
  for (int i = 0; i < N; ++i) {
    input[i] =
        std::sin(2.0 * std::numbers::pi_v<double> * 1000.0 * i / 44100.0);
  }

  auto output = r.Process(input, N);

  // Compute RMS of input and output (excluding boundary effects).
  int in_start = 500, in_end = N - 500;
  double in_rms = 0;
  for (int i = in_start; i < in_end; ++i) {
    in_rms += input[i] * input[i];
  }
  in_rms = std::sqrt(in_rms / (in_end - in_start));

  int out_start = static_cast<int>(500.0 * 48000.0 / 44100.0);
  int out_end = static_cast<int>(output.size()) - out_start;
  double out_rms = 0;
  for (int i = out_start; i < out_end; ++i) {
    out_rms += output[i] * output[i];
  }
  out_rms = std::sqrt(out_rms / (out_end - out_start));

  // RMS should be close (within 5%).
  EXPECT_NEAR(out_rms, in_rms, in_rms * 0.05);
}

TEST(ResamplerTest, EmptyInput) {
  Resampler r(44100, 48000, 1);
  auto output = r.Process(nullptr, 0);
  EXPECT_TRUE(output.empty());
}

}  // namespace
}  // namespace hibiki
