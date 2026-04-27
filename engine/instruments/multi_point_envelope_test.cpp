#include "engine/instruments/multi_point_envelope.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {
namespace {

// Helper: run envelope for N samples and return final value.
float runFor(MultiPointEnvelope& env, int samples, float sr = 44100.0f) {
  float val = 0;
  for (int i = 0; i < samples; ++i) {
    val = env.process(sr);
  }
  return val;
}

TEST(MultiPointEnvelopeTest, IdleByDefault) {
  MultiPointEnvelope env;
  EXPECT_TRUE(env.isIdle());
  EXPECT_FLOAT_EQ(env.getValue(), 0.0f);
}

TEST(MultiPointEnvelopeTest, ADSRCompatBasic) {
  MultiPointEnvelope env;
  // Very fast attack, decay to 0.7 sustain, medium release.
  env.setFromADSRTimes(0.001f, 0.01f, 0.7f, 0.01f);
  env.noteOn();
  EXPECT_FALSE(env.isIdle());

  // Run through attack + decay (fast enough that ~500 samples covers it).
  float val = runFor(env, 500);
  // Should be at sustain level (0.7).
  EXPECT_NEAR(val, 0.7f, 0.05f) << "Should reach sustain level";
  EXPECT_FALSE(env.isIdle());

  // Note off → release.
  env.noteOff();
  float release_val = runFor(env, 1000);
  EXPECT_NEAR(release_val, 0.0f, 0.01f) << "Should reach silence after release";
  EXPECT_TRUE(env.isIdle());
}

TEST(MultiPointEnvelopeTest, ADSRFromNormalized) {
  MultiPointEnvelope env;
  env.setFromADSR(0.0f, 0.0f, 0.7f, 0.0f);  // fastest A,D,R
  env.noteOn();
  float val = runFor(env, 100);
  EXPECT_NEAR(val, 0.7f, 0.05f);
}

TEST(MultiPointEnvelopeTest, CustomMultiPoint) {
  MultiPointEnvelope env;
  // 5 points: origin → peak → dip → sustain hold → release end
  std::vector<MultiPointEnvelope::Point> pts = {
      {0.0f, 0.0f, 0.0f},    // 0: origin
      {0.001f, 1.0f, 0.0f},  // 1: fast attack to peak
      {0.005f, 0.3f, 0.0f},  // 2: dip
      {0.001f, 0.8f, 0.0f},  // 3: rise to sustain (hold here)
      {0.01f, 0.0f, 0.0f},   // 4: release to zero
  };
  env.setPoints(pts, 3);  // sustain at point 3
  env.noteOn();

  // Run through segments 0→1→2→3.
  float val = runFor(env, 500);
  EXPECT_NEAR(val, 0.8f, 0.05f) << "Should hold at sustain point value";

  // Keep running — should stay at sustain.
  float still_sustain = runFor(env, 500);
  EXPECT_NEAR(still_sustain, 0.8f, 0.05f)
      << "Should not change during sustain hold";

  // Note off → release segment.
  env.noteOff();
  float released = runFor(env, 1000);
  EXPECT_NEAR(released, 0.0f, 0.01f) << "Should reach zero after release";
  EXPECT_TRUE(env.isIdle());
}

TEST(MultiPointEnvelopeTest, TensionCurveEaseIn) {
  MultiPointEnvelope env;
  // Two points with positive tension (ease-in: slow start, fast end).
  std::vector<MultiPointEnvelope::Point> pts = {
      {0.0f, 0.0f, 0.8f},  // tension = 0.8 → ease-in
      {0.01f, 1.0f, 0.0f},
  };
  env.setPoints(pts, -1);  // no sustain — play straight through
  env.noteOn();

  // At 50% time, value should be << 0.5 due to ease-in.
  float sr = 44100.0f;
  int half_samples = (int)(0.005f * sr);
  float mid_val = runFor(env, half_samples);
  EXPECT_LT(mid_val, 0.35f) << "Ease-in should be below midpoint at 50% time";
}

TEST(MultiPointEnvelopeTest, TensionCurveEaseOut) {
  MultiPointEnvelope env;
  std::vector<MultiPointEnvelope::Point> pts = {
      {0.0f, 0.0f, -0.8f},  // tension = -0.8 → ease-out
      {0.01f, 1.0f, 0.0f},
  };
  env.setPoints(pts, -1);
  env.noteOn();

  float sr = 44100.0f;
  int half_samples = (int)(0.005f * sr);
  float mid_val = runFor(env, half_samples);
  EXPECT_GT(mid_val, 0.65f) << "Ease-out should be above midpoint at 50% time";
}

TEST(MultiPointEnvelopeTest, SustainHoldsIndefinitely) {
  MultiPointEnvelope env;
  env.setFromADSRTimes(0.001f, 0.001f, 0.5f, 0.01f);
  env.noteOn();

  // Run for a long time without noteOff.
  float val = runFor(env, 44100);  // 1 second
  EXPECT_NEAR(val, 0.5f, 0.05f) << "Sustain should hold indefinitely";
  EXPECT_FALSE(env.isIdle());
}

TEST(MultiPointEnvelopeTest, NoteOffWithNoSustainMarker) {
  MultiPointEnvelope env;
  std::vector<MultiPointEnvelope::Point> pts = {
      {0.0f, 0.0f, 0.0f},
      {0.01f, 1.0f, 0.0f},
  };
  env.setPoints(pts, -1);  // no sustain marker
  env.noteOn();

  // Note off should go idle immediately (no release section).
  env.noteOff();
  EXPECT_TRUE(env.isIdle());
}

}  // namespace
}  // namespace hibiki
