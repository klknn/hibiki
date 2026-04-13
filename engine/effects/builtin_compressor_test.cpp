#include "engine/effects/builtin_compressor.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace hibiki {
namespace {

HostProcessContext MakeContext(double sr = 44100.0) {
  HostProcessContext ctx;
  ctx.sampleRate = sr;
  return ctx;
}

// --- Parameter mapping tests ---

TEST(BuiltinCompressorTest, ThresholdRange) {
  EXPECT_NEAR(BuiltinCompressor::normToThreshold(0.0), -60.0f, 0.1f);
  EXPECT_NEAR(BuiltinCompressor::normToThreshold(1.0), 0.0f, 0.1f);
}

TEST(BuiltinCompressorTest, RatioRange) {
  EXPECT_NEAR(BuiltinCompressor::normToRatio(0.0), 1.0f, 0.01f);
  EXPECT_GT(BuiltinCompressor::normToRatio(1.0), 100.0f);
}

TEST(BuiltinCompressorTest, AttackRange) {
  EXPECT_NEAR(BuiltinCompressor::normToAttack(0.0), 0.1f, 0.01f);
  EXPECT_NEAR(BuiltinCompressor::normToAttack(1.0), 100.0f, 1.0f);
}

TEST(BuiltinCompressorTest, ReleaseRange) {
  EXPECT_NEAR(BuiltinCompressor::normToRelease(0.0), 10.0f, 0.1f);
  EXPECT_NEAR(BuiltinCompressor::normToRelease(1.0), 1000.0f, 10.0f);
}

// --- Processing tests ---

TEST(BuiltinCompressorTest, BelowThresholdNoCompression) {
  BuiltinCompressor comp;
  comp.load("", 0, 44100.0);

  // Default: threshold=0 dB (norm=1.0), ratio=1:1 (norm=0.0)
  // Very quiet signal should pass through without compression
  constexpr int N = 256;
  float outL[N], outR[N];
  for (int i = 0; i < N; ++i) {
    outL[i] = outR[i] = 0.001f;  // ~-60 dB, well below threshold
  }

  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  comp.process(outs, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.001f, 1e-4f);
  }
}

TEST(BuiltinCompressorTest, AboveThresholdGainReduction) {
  BuiltinCompressor comp;
  comp.load("", 0, 44100.0);

  // Threshold = -20 dB, Ratio = 4:1
  comp.setParameterValue(BuiltinCompressor::PARAM_THRESHOLD,
                         (60.0 - 20.0) / 60.0);  // -20 dB
  comp.setParameterValue(BuiltinCompressor::PARAM_RATIO, 0.75);  // ~4:1
  comp.setParameterValue(BuiltinCompressor::PARAM_ATTACK, 0.0);  // fast
  comp.setParameterValue(BuiltinCompressor::PARAM_RELEASE, 0.0);

  constexpr int N = 4096;
  float outL[N], outR[N];
  float input_level = 0.5f;  // ~-6 dB, well above -20 dB threshold
  for (int i = 0; i < N; ++i) {
    outL[i] = outR[i] = input_level;
  }

  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  comp.process(outs, outs, N, ctx, events);

  // After compression, the last samples should be reduced
  float last = std::abs(outL[N - 1]);
  EXPECT_LT(last, input_level)
      << "Signal above threshold should be compressed";
}

TEST(BuiltinCompressorTest, TransferCurve) {
  BuiltinCompressor comp;
  comp.load("", 0, 44100.0);

  // Set threshold=-20, ratio=4:1
  comp.setParameterValue(BuiltinCompressor::PARAM_THRESHOLD,
                         (60.0 - 20.0) / 60.0);
  comp.setParameterValue(BuiltinCompressor::PARAM_RATIO, 0.75);

  // Below threshold: output should equal input
  EXPECT_NEAR(comp.computeOutputDb(-30.0f), -30.0f, 0.1f);

  // Above threshold: output should be compressed
  float out_at_0 = comp.computeOutputDb(0.0f);
  EXPECT_GT(out_at_0, -20.0f);
  EXPECT_LT(out_at_0, 0.0f);
}

TEST(BuiltinCompressorTest, DisabledBypass) {
  BuiltinCompressor comp;
  comp.load("", 0, 44100.0);

  comp.setParameterValue(BuiltinCompressor::PARAM_ENABLE, 0.0);

  constexpr int N = 64;
  float outL[N], outR[N];
  for (int i = 0; i < N; ++i) {
    outL[i] = outR[i] = 1.0f;
  }

  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  comp.process(outs, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 1.0f, 1e-5f);
  }
  EXPECT_NEAR(comp.getGainReductionDb(), 0.0f, 1e-5f);
}

TEST(BuiltinCompressorTest, NameAndPath) {
  BuiltinCompressor comp;
  EXPECT_EQ(comp.getName(), "Compressor");
  EXPECT_EQ(comp.getPath(), "builtin://compressor");
  EXPECT_FALSE(comp.isInstrument());
  EXPECT_EQ(comp.getParameterCount(), BuiltinCompressor::kTotalParams);
}

}  // namespace
}  // namespace hibiki
