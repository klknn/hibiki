#include "engine/effects/builtin_envelope_shaper.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace hibiki {
namespace {

class BuiltinEnvelopeShaperTest : public ::testing::Test {
 protected:
  BuiltinEnvelopeShaper shaper;
  static constexpr int kBlockSize = 256;
  float in_l[kBlockSize], in_r[kBlockSize];
  float out_l[kBlockSize], out_r[kBlockSize];
  float* inputs[2] = {in_l, in_r};
  float* outputs[2] = {out_l, out_r};
  HostProcessContext ctx{44100.0, 120.0, 4, 4, 0, 0.0};

  void SetUp() override {
    shaper.load("builtin://envelope_shaper", 0, 44100.0);
    std::fill(in_l, in_l + kBlockSize, 0.0f);
    std::fill(in_r, in_r + kBlockSize, 0.0f);
  }
};

TEST_F(BuiltinEnvelopeShaperTest, SilenceInSilenceOut) {
  shaper.process(inputs, outputs, kBlockSize, ctx, {});
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_NEAR(out_l[i], 0.0f, 1e-7f);
    EXPECT_NEAR(out_r[i], 0.0f, 1e-7f);
  }
}

TEST_F(BuiltinEnvelopeShaperTest, BypassWhenDisabled) {
  shaper.setParameterValue(BuiltinEnvelopeShaper::PARAM_ENABLE, 0.0);
  in_l[0] = 0.5f;
  in_r[0] = -0.3f;
  shaper.process(inputs, outputs, kBlockSize, ctx, {});
  EXPECT_FLOAT_EQ(out_l[0], 0.5f);
  EXPECT_FLOAT_EQ(out_r[0], -0.3f);
}

TEST_F(BuiltinEnvelopeShaperTest, ParameterCount) {
  EXPECT_EQ(shaper.getParameterCount(), 22);
}

TEST_F(BuiltinEnvelopeShaperTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(shaper.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Mix");
  EXPECT_TRUE(shaper.getParameterInfo(1, info));
  EXPECT_EQ(info.name, "Rate");
  EXPECT_TRUE(shaper.getParameterInfo(5, info));
  EXPECT_EQ(info.name, "Pt 0");
  EXPECT_TRUE(shaper.getParameterInfo(21, info));
  EXPECT_EQ(info.name, "Points");
  EXPECT_FALSE(shaper.getParameterInfo(22, info));
}

TEST_F(BuiltinEnvelopeShaperTest, Metadata) {
  EXPECT_EQ(shaper.getName(), "EnvShaper");
  EXPECT_EQ(shaper.getPath(), "builtin://envelope_shaper");
  EXPECT_FALSE(shaper.isInstrument());
}

TEST_F(BuiltinEnvelopeShaperTest, GainDuckingApplied) {
  // Set all points to 0 (full ducking)
  for (int i = 0; i < 16; ++i) {
    shaper.setParameterValue(BuiltinEnvelopeShaper::PARAM_POINT_Y_0 + i, 0.0);
  }
  shaper.setParameterValue(BuiltinEnvelopeShaper::PARAM_MIX, 1.0);

  // Fill input with constant signal
  std::fill(in_l, in_l + kBlockSize, 1.0f);
  std::fill(in_r, in_r + kBlockSize, 1.0f);
  shaper.process(inputs, outputs, kBlockSize, ctx, {});

  // Output should be near zero (full ducking)
  float energy = 0;
  for (int i = 0; i < kBlockSize; ++i) {
    energy += out_l[i] * out_l[i];
  }
  EXPECT_LT(energy, 0.01f);
}

TEST_F(BuiltinEnvelopeShaperTest, FullGainPassthrough) {
  // Set all points to 1 (no ducking)
  for (int i = 0; i < 16; ++i) {
    shaper.setParameterValue(BuiltinEnvelopeShaper::PARAM_POINT_Y_0 + i, 1.0);
  }
  shaper.setParameterValue(BuiltinEnvelopeShaper::PARAM_MIX, 1.0);

  std::fill(in_l, in_l + kBlockSize, 0.75f);
  shaper.process(inputs, outputs, kBlockSize, ctx, {});

  // Output should nearly equal input
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_NEAR(out_l[i], 0.75f, 0.01f);
  }
}

TEST_F(BuiltinEnvelopeShaperTest, RateMapping) {
  // Spot check rate table
  float beats = BuiltinEnvelopeShaper::normToRateBeats(0.0);
  EXPECT_GT(beats, 0.0f);  // Should be 1/16

  beats = BuiltinEnvelopeShaper::normToRateBeats(1.0);
  EXPECT_FLOAT_EQ(beats, 16.0f);  // 4 bars
}

}  // namespace
}  // namespace hibiki
