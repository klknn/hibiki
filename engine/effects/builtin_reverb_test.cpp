#include "engine/effects/builtin_reverb.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace hibiki {
namespace {

class BuiltinReverbTest : public ::testing::Test {
 protected:
  BuiltinReverb reverb;
  static constexpr int kBlockSize = 256;
  float in_l[kBlockSize], in_r[kBlockSize];
  float out_l[kBlockSize], out_r[kBlockSize];
  float* inputs[2] = {in_l, in_r};
  float* outputs[2] = {out_l, out_r};
  HostProcessContext ctx{44100.0, 120.0, 4, 4, 0, 0.0};

  void SetUp() override {
    reverb.load("builtin://reverb", 0, 44100.0);
    std::fill(in_l, in_l + kBlockSize, 0.0f);
    std::fill(in_r, in_r + kBlockSize, 0.0f);
  }
};

TEST_F(BuiltinReverbTest, SilenceInNearSilenceOut) {
  reverb.process(inputs, outputs, kBlockSize, ctx, {});
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_NEAR(out_l[i], 0.0f, 1e-5f);
    EXPECT_NEAR(out_r[i], 0.0f, 1e-5f);
  }
}

TEST_F(BuiltinReverbTest, ImpulseProducesReverb) {
  in_l[0] = 1.0f;
  in_r[0] = 1.0f;
  reverb.setParameterValue(BuiltinReverb::PARAM_MIX, 1.0);
  reverb.setParameterValue(BuiltinReverb::PARAM_ROOM_SIZE, 0.8);

  // Process the initial impulse
  reverb.process(inputs, outputs, kBlockSize, ctx, {});
  // Clear inputs, then process enough blocks for comb filters to output
  // (comb lengths ~1116-1617 samples, so need > ~7 blocks of 256)
  std::fill(in_l, in_l + kBlockSize, 0.0f);
  std::fill(in_r, in_r + kBlockSize, 0.0f);

  float energy = 0;
  for (int b = 0; b < 10; ++b) {
    reverb.process(inputs, outputs, kBlockSize, ctx, {});
    for (int i = 0; i < kBlockSize; ++i) {
      energy += out_l[i] * out_l[i] + out_r[i] * out_r[i];
    }
  }
  EXPECT_GT(energy, 1e-6f);
}

TEST_F(BuiltinReverbTest, BypassWhenDisabled) {
  reverb.setParameterValue(BuiltinReverb::PARAM_ENABLE, 0.0);
  in_l[0] = 0.5f;
  in_r[0] = -0.3f;
  reverb.process(inputs, outputs, kBlockSize, ctx, {});
  EXPECT_FLOAT_EQ(out_l[0], 0.5f);
  EXPECT_FLOAT_EQ(out_r[0], -0.3f);
}

TEST_F(BuiltinReverbTest, DryMixPassesThrough) {
  in_l[0] = 1.0f;
  reverb.setParameterValue(BuiltinReverb::PARAM_MIX, 0.0);
  reverb.process(inputs, outputs, kBlockSize, ctx, {});
  EXPECT_NEAR(out_l[0], 1.0f, 1e-5f);
}

TEST_F(BuiltinReverbTest, ParameterCount) {
  EXPECT_EQ(reverb.getParameterCount(), 8);
}

TEST_F(BuiltinReverbTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(reverb.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Room Size");
  EXPECT_FALSE(reverb.getParameterInfo(8, info));
}

TEST_F(BuiltinReverbTest, Metadata) {
  EXPECT_EQ(reverb.getName(), "Reverb");
  EXPECT_EQ(reverb.getPath(), "builtin://reverb");
  EXPECT_FALSE(reverb.isInstrument());
}

TEST_F(BuiltinReverbTest, PreDelayMapping) {
  EXPECT_NEAR(BuiltinReverb::normToPreDelayMs(0.0), 0.0f, 0.01f);
  EXPECT_NEAR(BuiltinReverb::normToPreDelayMs(1.0), 100.0f, 0.01f);
}

}  // namespace
}  // namespace hibiki
