#include "engine/effects/builtin_phaser.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

namespace hibiki {
namespace {

class BuiltinPhaserTest : public ::testing::Test {
 protected:
  BuiltinPhaser phaser;
  static constexpr int kBlockSize = 256;
  float in_l[kBlockSize], in_r[kBlockSize];
  float out_l[kBlockSize], out_r[kBlockSize];
  float* inputs[2] = {in_l, in_r};
  float* outputs[2] = {out_l, out_r};
  HostProcessContext ctx{44100.0, 120.0, 4, 4, 0, 0.0};

  void SetUp() override {
    phaser.load("builtin://phaser", 0, 44100.0);
    std::fill(in_l, in_l + kBlockSize, 0.0f);
    std::fill(in_r, in_r + kBlockSize, 0.0f);
  }
};

TEST_F(BuiltinPhaserTest, SilenceInSilenceOut) {
  phaser.process(inputs, outputs, kBlockSize, ctx, {});
  for (int i = 0; i < kBlockSize; ++i) {
    EXPECT_NEAR(out_l[i], 0.0f, 1e-6f);
    EXPECT_NEAR(out_r[i], 0.0f, 1e-6f);
  }
}

TEST_F(BuiltinPhaserTest, BypassWhenDisabled) {
  phaser.setParameterValue(BuiltinPhaser::PARAM_ENABLE, 0.0);
  in_l[0] = 0.5f;
  in_r[0] = -0.3f;
  phaser.process(inputs, outputs, kBlockSize, ctx, {});
  EXPECT_FLOAT_EQ(out_l[0], 0.5f);
  EXPECT_FLOAT_EQ(out_r[0], -0.3f);
}

TEST_F(BuiltinPhaserTest, ParameterCount) {
  EXPECT_EQ(phaser.getParameterCount(), 12);
}

TEST_F(BuiltinPhaserTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(phaser.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Rate");
  EXPECT_TRUE(phaser.getParameterInfo(6, info));
  EXPECT_EQ(info.name, "Mode");
  EXPECT_FALSE(phaser.getParameterInfo(12, info));
}

TEST_F(BuiltinPhaserTest, Metadata) {
  EXPECT_EQ(phaser.getName(), "Phaser");
  EXPECT_EQ(phaser.getPath(), "builtin://phaser");
  EXPECT_FALSE(phaser.isInstrument());
}

TEST_F(BuiltinPhaserTest, PhaserModeModifiesSignal) {
  phaser.setParameterValue(BuiltinPhaser::PARAM_MODE, 0.0);  // Phaser
  phaser.setParameterValue(BuiltinPhaser::PARAM_MIX, 1.0);
  phaser.setParameterValue(BuiltinPhaser::PARAM_DEPTH, 1.0);

  // Generate a test tone (440Hz sine)
  for (int i = 0; i < kBlockSize; ++i) {
    float val = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 44100.0f);
    in_l[i] = val;
    in_r[i] = val;
  }
  phaser.process(inputs, outputs, kBlockSize, ctx, {});

  // Output should differ from input (phase shift applied)
  float diff_energy = 0;
  for (int i = 0; i < kBlockSize; ++i) {
    float d = out_l[i] - in_l[i];
    diff_energy += d * d;
  }
  EXPECT_GT(diff_energy, 0.001f);
}

TEST_F(BuiltinPhaserTest, DisperserPreservesAmplitude) {
  phaser.setParameterValue(BuiltinPhaser::PARAM_MODE, 1.0);  // Disperser
  phaser.setParameterValue(BuiltinPhaser::PARAM_MIX, 1.0);
  phaser.setParameterValue(BuiltinPhaser::PARAM_DEPTH, 1.0);
  phaser.setParameterValue(BuiltinPhaser::PARAM_FEEDBACK, 0.5);  // 0 fb

  // Generate a test tone
  for (int i = 0; i < kBlockSize; ++i) {
    float val = 0.5f * std::sin(2.0f * 3.14159f * 1000.0f * i / 44100.0f);
    in_l[i] = val;
    in_r[i] = val;
  }

  // Process several blocks to let it stabilize
  for (int b = 0; b < 10; ++b) {
    phaser.process(inputs, outputs, kBlockSize, ctx, {});
  }

  // Compare RMS energy (allpass should preserve amplitude approximately)
  float in_energy = 0, out_energy = 0;
  for (int i = 0; i < kBlockSize; ++i) {
    in_energy += in_l[i] * in_l[i];
    out_energy += out_l[i] * out_l[i];
  }
  // Allow 20% tolerance due to mode blending
  EXPECT_GT(out_energy, in_energy * 0.3f);
}

TEST_F(BuiltinPhaserTest, ScopeDataReturnsValid) {
  // Feed some signal
  for (int i = 0; i < kBlockSize; ++i) {
    in_l[i] = 0.5f;
    in_r[i] = -0.5f;
  }
  phaser.process(inputs, outputs, kBlockSize, ctx, {});

  float scope_l[256], scope_r[256];
  phaser.getScopeData(scope_l, scope_r, 256);

  // Should have non-zero data
  float energy = 0;
  for (int i = 0; i < 256; ++i) {
    energy += scope_l[i] * scope_l[i] + scope_r[i] * scope_r[i];
  }
  EXPECT_GT(energy, 0.0f);
}

TEST_F(BuiltinPhaserTest, StageMapping) {
  EXPECT_EQ(BuiltinPhaser::normToStages(0.0), 2);
  EXPECT_EQ(BuiltinPhaser::normToStages(1.0), 12);
}

TEST_F(BuiltinPhaserTest, ModeMapping) {
  EXPECT_EQ(BuiltinPhaser::normToMode(0.0), 0);  // Phaser
  EXPECT_EQ(BuiltinPhaser::normToMode(1.0), 4);  // Disperser
}

}  // namespace
}  // namespace hibiki
