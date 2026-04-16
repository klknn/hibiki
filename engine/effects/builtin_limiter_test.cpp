#include "engine/effects/builtin_limiter.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace hibiki {
namespace {

class BuiltinLimiterTest : public ::testing::Test {
 protected:
  BuiltinLimiter limiter;
  static constexpr int kBlockSize = 256;
  float in_l[kBlockSize], in_r[kBlockSize];
  float out_l[kBlockSize], out_r[kBlockSize];
  float* inputs[2] = {in_l, in_r};
  float* outputs[2] = {out_l, out_r};
  HostProcessContext ctx{44100.0, 120.0, 4, 4, 0, 0.0};

  void SetUp() override {
    limiter.load("builtin://limiter", 0, 44100.0);
    std::fill(in_l, in_l + kBlockSize, 0.0f);
    std::fill(in_r, in_r + kBlockSize, 0.0f);
  }
};

TEST_F(BuiltinLimiterTest, SilenceInSilenceOut) {
  limiter.process(inputs, outputs, kBlockSize, ctx, {});
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_NEAR(out_l[i], 0.0f, 1e-7f);
    EXPECT_NEAR(out_r[i], 0.0f, 1e-7f);
  }
}

TEST_F(BuiltinLimiterTest, LoudSignalIsLimited) {
  // Send a very loud signal (amplitude 2.0)
  std::fill(in_l, in_l + kBlockSize, 2.0f);
  std::fill(in_r, in_r + kBlockSize, 2.0f);

  // Set ceiling to 0 dB (norm=1.0)
  limiter.setParameterValue(BuiltinLimiter::PARAM_CEILING, 1.0);
  limiter.setParameterValue(BuiltinLimiter::PARAM_GAIN, 0.0);

  // Process enough frames to get past lookahead
  limiter.process(inputs, outputs, kBlockSize, ctx, {});
  limiter.process(inputs, outputs, kBlockSize, ctx, {});

  // All output samples should be <= ceiling (1.0)
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_LE(std::abs(out_l[i]), 1.05f);  // small tolerance
    EXPECT_LE(std::abs(out_r[i]), 1.05f);
  }
}

TEST_F(BuiltinLimiterTest, GainReductionMetering) {
  std::fill(in_l, in_l + kBlockSize, 2.0f);
  std::fill(in_r, in_r + kBlockSize, 2.0f);
  limiter.setParameterValue(BuiltinLimiter::PARAM_CEILING, 1.0);

  limiter.process(inputs, outputs, kBlockSize, ctx, {});
  limiter.process(inputs, outputs, kBlockSize, ctx, {});

  float gr = limiter.getGainReductionDb();
  EXPECT_LT(gr, 0.0f);  // Should show gain reduction
}

TEST_F(BuiltinLimiterTest, BypassWhenDisabled) {
  limiter.setParameterValue(BuiltinLimiter::PARAM_ENABLE, 0.0);
  in_l[0] = 0.5f;
  in_r[0] = -0.3f;
  limiter.process(inputs, outputs, kBlockSize, ctx, {});
  EXPECT_FLOAT_EQ(out_l[0], 0.5f);
  EXPECT_FLOAT_EQ(out_r[0], -0.3f);
}

TEST_F(BuiltinLimiterTest, QuietSignalPassesThrough) {
  // Signal below ceiling should not be affected
  std::fill(in_l, in_l + kBlockSize, 0.3f);
  std::fill(in_r, in_r + kBlockSize, 0.3f);
  limiter.setParameterValue(BuiltinLimiter::PARAM_CEILING, 1.0);

  limiter.process(inputs, outputs, kBlockSize, ctx, {});
  // After lookahead samples, output should match input
  int la_samples =
      (int)(BuiltinLimiter::normToLookaheadMs(0.2) * 0.001f * 44100.0f);
  if (la_samples + 1 < kBlockSize) {
    EXPECT_NEAR(out_l[la_samples + 1], 0.3f, 0.01f);
  }
}

TEST_F(BuiltinLimiterTest, ParameterCount) {
  EXPECT_EQ(limiter.getParameterCount(), 6);
}

TEST_F(BuiltinLimiterTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(limiter.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Ceiling");
  EXPECT_FALSE(limiter.getParameterInfo(6, info));
}

TEST_F(BuiltinLimiterTest, Metadata) {
  EXPECT_EQ(limiter.getName(), "Limiter");
  EXPECT_EQ(limiter.getPath(), "builtin://limiter");
  EXPECT_FALSE(limiter.isInstrument());
}

TEST_F(BuiltinLimiterTest, CeilingMapping) {
  EXPECT_NEAR(BuiltinLimiter::normToCeilingDb(0.0), -12.0f, 0.01f);
  EXPECT_NEAR(BuiltinLimiter::normToCeilingDb(1.0), 0.0f, 0.01f);
}

}  // namespace
}  // namespace hibiki
