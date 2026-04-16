#include "engine/effects/builtin_eq.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {
namespace {

// Helper: create a HostProcessContext
HostProcessContext MakeContext(double sr = 44100.0) {
  HostProcessContext ctx;
  ctx.sampleRate = sr;
  return ctx;
}

// --- Parameter mapping tests ---

TEST(BuiltinEqTest, NormToFreqRange) {
  // norm=0 -> 20 Hz, norm=1 -> 20 kHz
  EXPECT_NEAR(BuiltinEq::normToFreq(0.0), 20.0f, 0.1f);
  EXPECT_NEAR(BuiltinEq::normToFreq(1.0), 20000.0f, 10.0f);
}

TEST(BuiltinEqTest, FreqToNormRoundTrip) {
  float freq = 1000.0f;
  double norm = BuiltinEq::freqToNorm(freq);
  float back = BuiltinEq::normToFreq(norm);
  EXPECT_NEAR(back, freq, 1.0f);
}

TEST(BuiltinEqTest, NormToQRange) {
  EXPECT_NEAR(BuiltinEq::normToQ(0.0), 0.1f, 0.01f);
  EXPECT_GT(BuiltinEq::normToQ(1.0), 10.0f);
}

TEST(BuiltinEqTest, QToNormRoundTrip) {
  float q = 0.707f;
  double norm = BuiltinEq::qToNorm(q);
  float back = BuiltinEq::normToQ(norm);
  EXPECT_NEAR(back, q, 0.01f);
}

// --- Processing tests ---

TEST(BuiltinEqTest, BypassPassthrough) {
  BuiltinEq eq;
  eq.load("", 0, 44100.0);

  // All bands OFF by default, so signal should pass through unchanged
  constexpr int N = 64;
  float inL[N], inR[N], outL[N], outR[N];
  for (int i = 0; i < N; ++i) {
    inL[i] = outL[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    inR[i] = outR[i] = inL[i];
  }

  float* ins[2] = {outL, outR};
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  eq.process(ins, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], inL[i], 1e-5f);
    EXPECT_NEAR(outR[i], inR[i], 1e-5f);
  }
}

TEST(BuiltinEqTest, BellBoostIncreasesLevel) {
  BuiltinEq eq;
  eq.load("", 0, 44100.0);

  // Enable band 0 as Bell, boost at 440 Hz
  eq.setParameterValue(0, 1.0);  // Band 0 type = BELL (norm >= 0.9)
  eq.setParameterValue(8, BuiltinEq::freqToNorm(440.0f));  // Freq
  eq.setParameterValue(16, 0.75);                          // Gain = +12 dB
  eq.setParameterValue(24, BuiltinEq::qToNorm(1.0f));      // Q

  constexpr int N = 1024;
  float outL[N], outR[N];
  for (int i = 0; i < N; ++i) {
    outL[i] = outR[i] = std::sin(2.0 * M_PI * 440.0 * i / 44100.0) * 0.5f;
  }

  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  eq.process(outs, outs, N, ctx, events);

  // After boost, peak should be larger than original 0.5
  float peak = 0;
  for (int i = N / 2; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.6f) << "Bell boost at 440 Hz should increase signal level";
}

TEST(BuiltinEqTest, DisableBypass) {
  BuiltinEq eq;
  eq.load("", 0, 44100.0);

  // Set a bell boost
  eq.setParameterValue(0, 1.0);
  eq.setParameterValue(16, 0.75);

  // Disable the EQ
  eq.setParameterValue(BuiltinEq::kNumBands * BuiltinEq::kParamsPerBand, 0.0);

  // getMagnitudeDb should return 0 when disabled
  EXPECT_NEAR(eq.getMagnitudeDb(1000.0f), 0.0f, 1e-5f);
}

TEST(BuiltinEqTest, ParameterCount) {
  BuiltinEq eq;
  EXPECT_EQ(eq.getParameterCount(), BuiltinEq::kTotalParams);
}

TEST(BuiltinEqTest, NameAndPath) {
  BuiltinEq eq;
  EXPECT_EQ(eq.getName(), "EQ Eight");
  EXPECT_EQ(eq.getPath(), "builtin://eq");
  EXPECT_FALSE(eq.isInstrument());
}

}  // namespace
}  // namespace hibiki
