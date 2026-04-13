#include "engine/core/fft.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <numbers>

namespace hibiki {
namespace {

TEST(SpectrumAnalyzerTest, DcSignalConcentratesInLowBins) {
  SpectrumAnalyzer analyzer;
  constexpr int kBins = 32;
  analyzer.init(kBins, 44100.0f);

  // Fill ring buffer with DC (constant value)
  float ring[SpectrumAnalyzer::kFftSize];
  for (int i = 0; i < SpectrumAnalyzer::kFftSize; ++i) {
    ring[i] = 1.0f;
  }

  std::atomic<float> out_db[kBins];
  for (int i = 0; i < kBins; ++i) out_db[i].store(-100.0f);

  analyzer.computeSpectrum(ring, out_db);

  // DC energy should be in the lowest bin, higher bins should be very low
  float low_bin = out_db[0].load();
  float high_bin = out_db[kBins - 1].load();
  EXPECT_GT(low_bin, high_bin + 20.0f)
      << "DC signal should concentrate in low frequency bins";
}

TEST(SpectrumAnalyzerTest, SineAtKnownFrequency) {
  SpectrumAnalyzer analyzer;
  constexpr int kBins = 64;
  constexpr float kSampleRate = 44100.0f;
  constexpr float kFreq = 1000.0f;
  analyzer.init(kBins, kSampleRate);

  // Generate 1 kHz sine into ring buffer
  float ring[SpectrumAnalyzer::kFftSize];
  for (int i = 0; i < SpectrumAnalyzer::kFftSize; ++i) {
    ring[i] =
        std::sin(2.0f * std::numbers::pi_v<float> * kFreq * i / kSampleRate);
  }

  std::atomic<float> out_db[kBins];
  for (int i = 0; i < kBins; ++i) out_db[i].store(-100.0f);

  analyzer.computeSpectrum(ring, out_db);

  // Find peak bin
  int peak_bin = 0;
  float peak_val = -200.0f;
  for (int i = 0; i < kBins; ++i) {
    float v = out_db[i].load();
    if (v > peak_val) {
      peak_val = v;
      peak_bin = i;
    }
  }

  // Peak should be above -10 dB for a full-amplitude sine
  EXPECT_GT(peak_val, -15.0f);
  // Peak bin should be roughly in the middle range (1 kHz out of 20-20k)
  EXPECT_GT(peak_bin, 0);
  EXPECT_LT(peak_bin, kBins - 1);
}

TEST(SpectrumAnalyzerTest, AccumulateToRing) {
  SpectrumAnalyzer analyzer;
  analyzer.init(32, 44100.0f);

  float ring[SpectrumAnalyzer::kFftSize] = {};
  float L[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  float R[4] = {1.0f, 2.0f, 3.0f, 4.0f};

  analyzer.accumulateToRing(L, R, 4, ring);

  // Mono mix: (L+R)/2
  EXPECT_NEAR(ring[0], 1.0f, 1e-5f);
  EXPECT_NEAR(ring[1], 2.0f, 1e-5f);
  EXPECT_NEAR(ring[2], 3.0f, 1e-5f);
  EXPECT_NEAR(ring[3], 4.0f, 1e-5f);
}

}  // namespace
}  // namespace hibiki
