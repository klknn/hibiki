#include "engine/effects/builtin_delay.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace hibiki {
namespace {

class BuiltinDelayTest : public ::testing::Test {
 protected:
  BuiltinDelay delay;
  static constexpr int kBlockSize = 256;
  float in_l[kBlockSize], in_r[kBlockSize];
  float out_l[kBlockSize], out_r[kBlockSize];
  float* inputs[2] = {in_l, in_r};
  float* outputs[2] = {out_l, out_r};
  HostProcessContext ctx{44100.0, 120.0, 4, 4, 0, 0.0};

  void SetUp() override {
    delay.load("builtin://delay", 0, 44100.0);
    std::fill(in_l, in_l + kBlockSize, 0.0f);
    std::fill(in_r, in_r + kBlockSize, 0.0f);
  }
};

TEST_F(BuiltinDelayTest, SilenceInSilenceOut) {
  delay.process(inputs, outputs, kBlockSize, ctx, {});
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_NEAR(out_l[i], 0.0f, 1e-7f);
    EXPECT_NEAR(out_r[i], 0.0f, 1e-7f);
  }
}

TEST_F(BuiltinDelayTest, ImpulseProducesDelayedOutput) {
  in_l[0] = 1.0f;
  in_r[0] = 1.0f;
  // Set short delay: ~5ms
  delay.setParameterValue(BuiltinDelay::PARAM_TIME_L, 0.22);  // ~5ms
  delay.setParameterValue(BuiltinDelay::PARAM_TIME_R, 0.22);
  delay.setParameterValue(BuiltinDelay::PARAM_MIX, 0.5);
  delay.setParameterValue(BuiltinDelay::PARAM_FEEDBACK, 0.0);

  // Process enough blocks to see the delayed impulse
  delay.process(inputs, outputs, kBlockSize, ctx, {});

  // The first sample should be mix'd dry (0.5*1.0 + 0.5*0.0 = 0.5)
  EXPECT_NEAR(out_l[0], 0.5f, 0.01f);

  // After the delay, we should see the wet impulse
  int delay_samples =
      (int)(BuiltinDelay::normToTimeMs(0.22) * 0.001f * 44100.0f);
  if (delay_samples < kBlockSize) {
    EXPECT_GT(std::abs(out_l[delay_samples]), 0.1f);
  }
}

TEST_F(BuiltinDelayTest, BypassWhenDisabled) {
  delay.setParameterValue(BuiltinDelay::PARAM_ENABLE, 0.0);
  in_l[0] = 0.5f;
  in_r[0] = -0.3f;
  delay.process(inputs, outputs, kBlockSize, ctx, {});
  EXPECT_FLOAT_EQ(out_l[0], 0.5f);
  EXPECT_FLOAT_EQ(out_r[0], -0.3f);
}

TEST_F(BuiltinDelayTest, ParameterCount) {
  EXPECT_EQ(delay.getParameterCount(), 8);
}

TEST_F(BuiltinDelayTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(delay.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Time L");
  EXPECT_FALSE(delay.getParameterInfo(8, info));
}

TEST_F(BuiltinDelayTest, Metadata) {
  EXPECT_EQ(delay.getName(), "Delay");
  EXPECT_EQ(delay.getPath(), "builtin://delay");
  EXPECT_FALSE(delay.isInstrument());
}

TEST_F(BuiltinDelayTest, FeedbackProducesRepeats) {
  in_l[0] = 1.0f;
  delay.setParameterValue(BuiltinDelay::PARAM_TIME_L, 0.1);  // very short
  delay.setParameterValue(BuiltinDelay::PARAM_FEEDBACK, 0.8);
  delay.setParameterValue(BuiltinDelay::PARAM_MIX, 1.0);

  // Process several blocks
  delay.process(inputs, outputs, kBlockSize, ctx, {});
  std::fill(in_l, in_l + kBlockSize, 0.0f);
  delay.process(inputs, outputs, kBlockSize, ctx, {});
  delay.process(inputs, outputs, kBlockSize, ctx, {});

  // With high feedback, output should still have energy after many samples
  float energy = 0;
  for (int i = 0; i < kBlockSize; ++i) {
    energy += out_l[i] * out_l[i];
  }
  EXPECT_GT(energy, 0.0f);
}

}  // namespace
}  // namespace hibiki
