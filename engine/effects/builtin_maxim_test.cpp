#include "engine/effects/builtin_maxim.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "engine/core/math.hpp"

namespace hibiki {
namespace {

class BuiltinMaximTest : public ::testing::Test {
 protected:
  BuiltinMaxim maxim;
  static constexpr int kBlockSize = 256;
  float in_l[kBlockSize], in_r[kBlockSize];
  float out_l[kBlockSize], out_r[kBlockSize];
  float* inputs[2] = {in_l, in_r};
  float* outputs[2] = {out_l, out_r};
  HostProcessContext ctx{44100.0, 120.0, 4, 4, 0, 0.0};

  void SetUp() override {
    maxim.load("builtin://maxim", 0, 44100.0);
    std::fill(in_l, in_l + kBlockSize, 0.0f);
    std::fill(in_r, in_r + kBlockSize, 0.0f);
  }
};

TEST_F(BuiltinMaximTest, SilenceInSilenceOut) {
  maxim.process(inputs, outputs, kBlockSize, ctx, {});
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_NEAR(out_l[i], 0.0f, 1e-7f);
    EXPECT_NEAR(out_r[i], 0.0f, 1e-7f);
  }
}

TEST_F(BuiltinMaximTest, LoudSignalIsLimited) {
  // Send a very loud signal (amplitude 2.0)
  std::fill(in_l, in_l + kBlockSize, 2.0f);
  std::fill(in_r, in_r + kBlockSize, 2.0f);

  // Set Master ceiling to -1 dB (norm = 11.0 / 12.0)
  maxim.setParameterValue(BuiltinMaxim::PARAM_MASTER_CEILING, 11.0 / 12.0);

  // Process enough frames to get past lookahead
  maxim.process(inputs, outputs, kBlockSize, ctx, {});
  maxim.process(inputs, outputs, kBlockSize, ctx, {});

  // All output samples should be <= ceiling (-1 dB = 0.891 amplitude)
  float expected_ceiling = std::pow(10.0f, -1.0f / 20.0f);
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_LE(std::abs(out_l[i]), expected_ceiling * 1.05f);  // small tolerance
    EXPECT_LE(std::abs(out_r[i]), expected_ceiling * 1.05f);
  }
}

TEST_F(BuiltinMaximTest, CrossoverFiltersPreserveSignal) {
  // All bands default to flat gain/uncompressed/no limiting/no saturation.
  // Master ceiling is set to 1.0 (0 dB).
  for (int b = 0; b < 4; ++b) {
    int offset = 4 + b * 10;
    maxim.setParameterValue(offset + 9, 1.0);  // 0 dB ceiling
  }
  maxim.setParameterValue(BuiltinMaxim::PARAM_LOOKAHEAD,
                          0.0);  // minimal lookahead

  // Process a few blocks with continuous sine wave input
  for (int block = 0; block < 3; ++block) {
    ctx.continuousTimeSamples = block * kBlockSize;
    for (int i = 0; i < kBlockSize; ++i) {
      int sample_idx = block * kBlockSize + i;
      in_l[i] = 0.5f * std::sin(2.0f * (float)hibiki::pi * 1000.0f *
                                (float)sample_idx / 44100.0f);
      in_r[i] = 0.5f * std::cos(2.0f * (float)hibiki::pi * 1000.0f *
                                (float)sample_idx / 44100.0f);
    }
    maxim.process(inputs, outputs, kBlockSize, ctx, {});
  }

  // Crossover should reconstruct perfectly, meaning magnitude is preserved.
  // After 3 blocks (768 samples), any initial filter startup transients have
  // decayed. We check that the peak amplitude in the final block is very close
  // to 0.5.
  float peak_l = 0.0f;
  float peak_r = 0.0f;
  for (int i = 0; i < kBlockSize; ++i) {
    peak_l = std::max(peak_l, std::abs(out_l[i]));
    peak_r = std::max(peak_r, std::abs(out_r[i]));
  }

  EXPECT_NEAR(peak_l, 0.5f, 0.01f);
  EXPECT_NEAR(peak_r, 0.5f, 0.01f);
}

TEST_F(BuiltinMaximTest, BypassWhenDisabled) {
  maxim.setParameterValue(BuiltinMaxim::PARAM_ENABLE, 0.0);
  in_l[0] = 0.5f;
  in_r[0] = -0.3f;
  maxim.process(inputs, outputs, kBlockSize, ctx, {});
  EXPECT_FLOAT_EQ(out_l[0], 0.5f);
  EXPECT_FLOAT_EQ(out_r[0], -0.3f);
}

TEST_F(BuiltinMaximTest, ParameterCount) {
  EXPECT_EQ(maxim.getParameterCount(), 44);
}

TEST_F(BuiltinMaximTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(maxim.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "LowXover");
  EXPECT_TRUE(maxim.getParameterInfo(4, info));
  EXPECT_EQ(info.name, "LowPreGain");
  EXPECT_TRUE(maxim.getParameterInfo(43, info));
  EXPECT_EQ(info.name, "MasterCeiling");
  EXPECT_FALSE(maxim.getParameterInfo(44, info));
}

TEST_F(BuiltinMaximTest, Metadata) {
  EXPECT_EQ(maxim.getName(), "Maxim");
  EXPECT_EQ(maxim.getPath(), "builtin://maxim");
  EXPECT_FALSE(maxim.isInstrument());
}

}  // namespace
}  // namespace hibiki
