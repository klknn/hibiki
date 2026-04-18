#include "engine/effects/builtin_compressor.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

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
                         (60.0 - 20.0) / 60.0);                  // -20 dB
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
  EXPECT_LT(last, input_level) << "Signal above threshold should be compressed";
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

TEST(BuiltinCompressorTest, UpwardCompressionBoostsQuietSignal) {
  BuiltinCompressor comp;
  comp.load("", 0, 44100.0);

  // Set downward threshold high (effectively off), enable upward comp
  comp.setParameterValue(BuiltinCompressor::PARAM_THRESHOLD, 1.0);  // 0 dB
  comp.setParameterValue(BuiltinCompressor::PARAM_RATIO, 0.0);      // 1:1 down
  comp.setParameterValue(BuiltinCompressor::PARAM_ATTACK, 0.0);     // fast
  comp.setParameterValue(BuiltinCompressor::PARAM_RELEASE, 0.0);
  // Up threshold = -10 dB -> norm = (−10+60)/72 ≈ 0.694
  comp.setParameterValue(BuiltinCompressor::PARAM_UP_THRESHOLD, 50.0 / 72.0);
  // Up ratio ~4:1 -> norm = 0.75
  comp.setParameterValue(BuiltinCompressor::PARAM_UP_RATIO, 0.75);

  constexpr int N = 4096;
  float outL[N], outR[N];
  float input_level = 0.01f;  // ~-40 dB, well below -10 dB up_threshold
  for (int i = 0; i < N; ++i) {
    outL[i] = outR[i] = input_level;
  }

  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  comp.process(outs, outs, N, ctx, events);

  // After upward compression, last samples should be louder
  float last = std::abs(outL[N - 1]);
  EXPECT_GT(last, input_level)
      << "Quiet signal below up_threshold should be boosted";
}

TEST(BuiltinCompressorTest, DefaultUpwardHasNoEffect) {
  BuiltinCompressor comp;
  comp.load("", 0, 44100.0);

  // Defaults: up_threshold=-60 dB, up_ratio=1:1 -> no upward effect
  constexpr int N = 256;
  float outL[N], outR[N];
  for (int i = 0; i < N; ++i) {
    outL[i] = outR[i] = 0.001f;  // -60 dB
  }

  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  comp.process(outs, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.001f, 1e-4f);
  }
}

TEST(BuiltinCompressorTest, SidechainDetectionFollowsSidechainSignal) {
  BuiltinCompressor comp;
  comp.load("", 0, 44100.0);

  // Set threshold = -20 dB, ratio = 4:1, fast attack/release
  comp.setParameterValue(BuiltinCompressor::PARAM_THRESHOLD,
                         (60.0 - 20.0) / 60.0);                  // -20 dB
  comp.setParameterValue(BuiltinCompressor::PARAM_RATIO, 0.75);  // ~4:1
  comp.setParameterValue(BuiltinCompressor::PARAM_ATTACK, 0.0);  // fast
  comp.setParameterValue(BuiltinCompressor::PARAM_RELEASE, 0.0);

  constexpr int N = 4096;

  // Main signal: very quiet (-60 dB), well below threshold
  float mainL[N], mainR[N];
  float main_level = 0.001f;
  for (int i = 0; i < N; ++i) {
    mainL[i] = mainR[i] = main_level;
  }

  // Sidechain signal: loud (-6 dB), well above threshold
  float scL[N], scR[N];
  for (int i = 0; i < N; ++i) {
    scL[i] = scR[i] = 0.5f;
  }

  float* outs[2] = {mainL, mainR};
  float* sc[2] = {scL, scR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  comp.process(outs, outs, N, ctx, events, sc);

  // The main signal should be attenuated because the sidechain triggered
  // gain reduction, even though the main signal itself is below threshold.
  float last = std::abs(mainL[N - 1]);
  EXPECT_LT(last, main_level)
      << "Sidechain detection should compress main signal based on SC level";
}

TEST(BuiltinCompressorTest, NullptrSidechainBackwardCompatible) {
  // Run two compressors: one with explicit nullptr, one without sidechain arg
  BuiltinCompressor comp1, comp2;
  comp1.load("", 0, 44100.0);
  comp2.load("", 0, 44100.0);

  comp1.setParameterValue(BuiltinCompressor::PARAM_THRESHOLD,
                          (60.0 - 20.0) / 60.0);
  comp1.setParameterValue(BuiltinCompressor::PARAM_RATIO, 0.75);
  comp2.setParameterValue(BuiltinCompressor::PARAM_THRESHOLD,
                          (60.0 - 20.0) / 60.0);
  comp2.setParameterValue(BuiltinCompressor::PARAM_RATIO, 0.75);

  constexpr int N = 2048;
  float out1L[N], out1R[N], out2L[N], out2R[N];
  for (int i = 0; i < N; ++i) {
    out1L[i] = out1R[i] = 0.3f;
    out2L[i] = out2R[i] = 0.3f;
  }

  float* outs1[2] = {out1L, out1R};
  float* outs2[2] = {out2L, out2R};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;

  comp1.process(outs1, outs1, N, ctx, events, nullptr);
  comp2.process(outs2, outs2, N, ctx, events);  // no sidechain arg

  // Both should produce identical output
  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(out1L[i], out2L[i], 1e-6f) << "Sample " << i;
  }
}

}  // namespace
}  // namespace hibiki
