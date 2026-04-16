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
}

TEST_F(BuiltinHottTest, ParameterCount) {
  EXPECT_EQ(hott.getParameterCount(), BuiltinHott::kTotalParams);
}

TEST_F(BuiltinHottTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(hott.getParameterInfo(0, info));
  EXPECT_EQ(info.id, 0);
  EXPECT_EQ(std::string(info.name), "LowXover");

  EXPECT_TRUE(hott.getParameterInfo(2, info));
  EXPECT_EQ(std::string(info.name), "Amount");

  EXPECT_FALSE(hott.getParameterInfo(-1, info));
  EXPECT_FALSE(hott.getParameterInfo(9, info));
}

TEST_F(BuiltinHottTest, Identity) {
  EXPECT_EQ(hott.getName(), "Hott");
  EXPECT_EQ(hott.getPath(), "builtin://hott");
  EXPECT_FALSE(hott.isInstrument());
}

TEST_F(BuiltinHottTest, FrequencyMapping) {
  // normToFreq: logarithmic mapping
  EXPECT_NEAR(BuiltinHott::normToFreq(0.0, 20.0f, 500.0f), 20.0f, 0.1f);
  EXPECT_NEAR(BuiltinHott::normToFreq(1.0, 20.0f, 500.0f), 500.0f, 0.1f);
  // Midpoint should be geometric mean
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
  // Process silence — output should remain silent
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
  // Process a moderate signal — should produce output (not silence)
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

  // Output should not be silent
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

  // Should be unmodified when disabled
  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(inL[i], origL[i], 1e-6f);
  }
}

TEST_F(BuiltinHottTest, BandGainReductionMetering) {
  // Process a loud signal
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

  // At least one band should show gain reduction
  bool any_gr = false;
  for (int b = 0; b < 3; ++b) {
    if (hott.getBandGainReduction(b) < -0.1f) any_gr = true;
  }
  EXPECT_TRUE(any_gr);
}

}  // namespace
}  // namespace hibiki
