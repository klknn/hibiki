#include "engine/effects/builtin_hott.hpp"

#include <cmath>
#include <vector>

#include "gtest/gtest.h"

namespace hibiki {
namespace {

class BuiltinHottTest : public ::testing::Test {
 protected:
  BuiltinHott hott;
  void SetUp() override { hott.load("builtin://hott", 0, 44100.0); }
};

TEST_F(BuiltinHottTest, DefaultParameters) {
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_AMOUNT), 1.0, 0.01);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_TIME), 1.0, 0.01);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_OUTPUT), 0.5, 0.01);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_ENABLE), 1.0, 0.01);
  // Soft knee and RMS on by default
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_SOFT_KNEE), 1.0, 0.01);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_RMS_MODE), 1.0, 0.01);
  // Per-band input gains ~5.2 dB (norm 0.608)
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_LOW_IN), 0.608, 0.01);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_MID_IN), 0.608, 0.01);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_HIGH_IN), 0.608, 0.01);
}

TEST_F(BuiltinHottTest, DefaultThresholds) {
  // [A]bove — downward thresholds: Low -33.8, Mid -30.2, High -35.5
  float low_down_db = BuiltinCompressor::normToThreshold(
      hott.getParameterValue(BuiltinHott::PARAM_LOW_DOWN_THRESH));
  float mid_down_db = BuiltinCompressor::normToThreshold(
      hott.getParameterValue(BuiltinHott::PARAM_MID_DOWN_THRESH));
  float hi_down_db = BuiltinCompressor::normToThreshold(
      hott.getParameterValue(BuiltinHott::PARAM_HIGH_DOWN_THRESH));
  EXPECT_NEAR(low_down_db, -33.8f, 0.1f);
  EXPECT_NEAR(mid_down_db, -30.2f, 0.1f);
  EXPECT_NEAR(hi_down_db, -35.5f, 0.1f);

  // [B]elow — upward thresholds: Low -40.8, Mid -41.8, High -40.8
  float low_up_db = BuiltinCompressor::normToUpThreshold(
      hott.getParameterValue(BuiltinHott::PARAM_LOW_UP_THRESH));
  float mid_up_db = BuiltinCompressor::normToUpThreshold(
      hott.getParameterValue(BuiltinHott::PARAM_MID_UP_THRESH));
  float hi_up_db = BuiltinCompressor::normToUpThreshold(
      hott.getParameterValue(BuiltinHott::PARAM_HIGH_UP_THRESH));
  EXPECT_NEAR(low_up_db, -40.8f, 0.1f);
  EXPECT_NEAR(mid_up_db, -41.8f, 0.1f);
  EXPECT_NEAR(hi_up_db, -40.8f, 0.1f);
}

TEST_F(BuiltinHottTest, DefaultAttackRelease) {
  // Attack: norm -> ms = 0.1 * 1000^norm
  float low_att = 0.1f * std::pow(1000.0f, (float)hott.getParameterValue(
                                               BuiltinHott::PARAM_LOW_ATTACK));
  float mid_att = 0.1f * std::pow(1000.0f, (float)hott.getParameterValue(
                                               BuiltinHott::PARAM_MID_ATTACK));
  float hi_att = 0.1f * std::pow(1000.0f, (float)hott.getParameterValue(
                                              BuiltinHott::PARAM_HIGH_ATTACK));
  EXPECT_NEAR(low_att, 47.8f, 0.5f);
  EXPECT_NEAR(mid_att, 22.4f, 0.5f);
  EXPECT_NEAR(hi_att, 13.5f, 0.5f);

  // Release: norm -> ms = 10 * 100^norm
  float low_rel = 10.0f * std::pow(100.0f, (float)hott.getParameterValue(
                                               BuiltinHott::PARAM_LOW_RELEASE));
  float mid_rel = 10.0f * std::pow(100.0f, (float)hott.getParameterValue(
                                               BuiltinHott::PARAM_MID_RELEASE));
  float hi_rel = 10.0f * std::pow(100.0f, (float)hott.getParameterValue(
                                              BuiltinHott::PARAM_HIGH_RELEASE));
  EXPECT_NEAR(low_rel, 282.0f, 1.0f);
  EXPECT_NEAR(mid_rel, 282.0f, 1.0f);
  EXPECT_NEAR(hi_rel, 132.0f, 1.0f);
}

TEST_F(BuiltinHottTest, ParameterCount) {
  EXPECT_EQ(hott.getParameterCount(), BuiltinHott::kTotalParams);
  EXPECT_EQ(BuiltinHott::kTotalParams, 32);
}

TEST_F(BuiltinHottTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(hott.getParameterInfo(0, info));
  EXPECT_EQ(info.id, 0);
  EXPECT_EQ(std::string(info.name), "LowXover");

  EXPECT_TRUE(hott.getParameterInfo(2, info));
  EXPECT_EQ(std::string(info.name), "Amount");

  // New params
  EXPECT_TRUE(hott.getParameterInfo(15, info));
  EXPECT_EQ(std::string(info.name), "SoftKnee");
  EXPECT_TRUE(hott.getParameterInfo(16, info));
  EXPECT_EQ(std::string(info.name), "RmsMode");
  EXPECT_TRUE(hott.getParameterInfo(17, info));
  EXPECT_EQ(std::string(info.name), "LowAttack");
  EXPECT_TRUE(hott.getParameterInfo(29, info));
  EXPECT_EQ(std::string(info.name), "LowIn");
  EXPECT_TRUE(hott.getParameterInfo(31, info));
  EXPECT_EQ(std::string(info.name), "HighIn");

  EXPECT_FALSE(hott.getParameterInfo(-1, info));
  EXPECT_FALSE(hott.getParameterInfo(32, info));
}

TEST_F(BuiltinHottTest, Identity) {
  EXPECT_EQ(hott.getName(), "Hott");
  EXPECT_EQ(hott.getPath(), "builtin://hott");
  EXPECT_FALSE(hott.isInstrument());
}

TEST_F(BuiltinHottTest, FrequencyMapping) {
  EXPECT_NEAR(BuiltinHott::normToFreq(0.0, 20.0f, 500.0f), 20.0f, 0.1f);
  EXPECT_NEAR(BuiltinHott::normToFreq(1.0, 20.0f, 500.0f), 500.0f, 0.1f);
  float mid = BuiltinHott::normToFreq(0.5, 20.0f, 500.0f);
  EXPECT_GT(mid, 90.0f);
  EXPECT_LT(mid, 110.0f);
}

TEST_F(BuiltinHottTest, DbMapping) {
  EXPECT_NEAR(BuiltinHott::normToDb24(0.0), -24.0f, 0.01f);
  EXPECT_NEAR(BuiltinHott::normToDb24(0.5), 0.0f, 0.01f);
  EXPECT_NEAR(BuiltinHott::normToDb24(1.0), 24.0f, 0.01f);
}

TEST_F(BuiltinHottTest, SilencePassthrough) {
  int N = 512;
  std::vector<float> inL(N, 0.0f), inR(N, 0.0f);
  float* ins[] = {inL.data(), inR.data()};
  float* outs[] = {inL.data(), inR.data()};
  HostProcessContext ctx;
  ctx.sampleRate = 44100.0;
  ctx.tempo = 120.0;
  hott.process(ins, outs, N, ctx, {});

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(inL[i], 0.0f, 1e-6f);
    EXPECT_NEAR(inR[i], 0.0f, 1e-6f);
  }
}

TEST_F(BuiltinHottTest, ProcessesSignal) {
  int N = 4096;
  std::vector<float> inL(N), inR(N);
  for (int i = 0; i < N; ++i) {
    float t = i / 44100.0f;
    inL[i] = 0.3f * std::sin(2.0f * M_PI * 440.0f * t);
    inR[i] = inL[i];
  }
  float* bufs[] = {inL.data(), inR.data()};
  HostProcessContext ctx;
  ctx.sampleRate = 44100.0;
  ctx.tempo = 120.0;
  hott.process(bufs, bufs, N, ctx, {});

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(inL[i]));
  }
  EXPECT_GT(peak, 0.01f);
}

TEST_F(BuiltinHottTest, DisabledBypass) {
  hott.setParameterValue(BuiltinHott::PARAM_ENABLE, 0.0);

  int N = 512;
  std::vector<float> inL(N), inR(N), origL(N), origR(N);
  for (int i = 0; i < N; ++i) {
    float t = i / 44100.0f;
    inL[i] = origL[i] = 0.5f * std::sin(2.0f * M_PI * 1000.0f * t);
    inR[i] = origR[i] = inL[i];
  }
  float* bufs[] = {inL.data(), inR.data()};
  HostProcessContext ctx;
  ctx.sampleRate = 44100.0;
  ctx.tempo = 120.0;
  hott.process(bufs, bufs, N, ctx, {});

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(inL[i], origL[i], 1e-6f);
  }
}

TEST_F(BuiltinHottTest, BandGainReductionMetering) {
  int N = 4096;
  std::vector<float> inL(N), inR(N);
  for (int i = 0; i < N; ++i) {
    float t = i / 44100.0f;
    inL[i] = 0.8f * std::sin(2.0f * M_PI * 1000.0f * t);
    inR[i] = inL[i];
  }
  float* bufs[] = {inL.data(), inR.data()};
  HostProcessContext ctx;
  ctx.sampleRate = 44100.0;
  ctx.tempo = 120.0;
  hott.process(bufs, bufs, N, ctx, {});

  bool any_gr = false;
  for (int b = 0; b < 3; ++b) {
    if (hott.getBandGainReduction(b) < -0.1f) any_gr = true;
  }
  EXPECT_TRUE(any_gr);
}

TEST_F(BuiltinHottTest, SoftKneeToggle) {
  // Starts on by default
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_SOFT_KNEE), 1.0, 0.01);
  // Toggle off
  hott.setParameterValue(BuiltinHott::PARAM_SOFT_KNEE, 0.0);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_SOFT_KNEE), 0.0, 0.01);
  // Toggle back on
  hott.setParameterValue(BuiltinHott::PARAM_SOFT_KNEE, 1.0);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_SOFT_KNEE), 1.0, 0.01);
}

TEST_F(BuiltinHottTest, RmsModeToggle) {
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_RMS_MODE), 1.0, 0.01);
  hott.setParameterValue(BuiltinHott::PARAM_RMS_MODE, 0.0);
  EXPECT_NEAR(hott.getParameterValue(BuiltinHott::PARAM_RMS_MODE), 0.0, 0.01);

  // Process should still work in peak mode
  int N = 512;
  std::vector<float> inL(N, 0.0f), inR(N, 0.0f);
  float* bufs[] = {inL.data(), inR.data()};
  HostProcessContext ctx;
  ctx.sampleRate = 44100.0;
  ctx.tempo = 120.0;
  hott.process(bufs, bufs, N, ctx, {});
}

}  // namespace
}  // namespace hibiki
