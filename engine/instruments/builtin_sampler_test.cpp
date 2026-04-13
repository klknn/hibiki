#include "engine/instruments/builtin_sampler.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <vector>

#include "engine/test_utils.hpp"

namespace hibiki {
namespace {

HostProcessContext MakeContext(double sr = 44100.0) {
  HostProcessContext ctx;
  ctx.sampleRate = sr;
  return ctx;
}

TEST(BuiltinSamplerTest, SilenceWithoutSample) {
  BuiltinSampler sampler;
  sampler.load("", 0, 44100.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 60;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  sampler.process(nullptr, outs, N, ctx, events);

  // No sample loaded, so output should be silence
  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-10f);
  }
}

TEST(BuiltinSamplerTest, LoadSampleAndPlay) {
  BuiltinSampler sampler;
  sampler.load("", 0, 44100.0);

  std::string wav_path = hibiki::find_test_file("testdata/loop140.wav");
  ASSERT_TRUE(sampler.loadSample(wav_path));
  EXPECT_FALSE(sampler.getWaveformSummary().empty());

  constexpr int N = 512;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 60;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  sampler.process(nullptr, outs, N, ctx, events);

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.001f) << "Playing loaded sample should produce output";
}

TEST(BuiltinSamplerTest, LoadSampleNotFound) {
  BuiltinSampler sampler;
  sampler.load("", 0, 44100.0);
  EXPECT_FALSE(sampler.loadSample("/nonexistent/file.wav"));
}

TEST(BuiltinSamplerTest, WaveformSummarySize) {
  BuiltinSampler sampler;
  sampler.load("", 0, 44100.0);

  std::string wav_path = hibiki::find_test_file("testdata/loop140.wav");
  ASSERT_TRUE(sampler.loadSample(wav_path));
  EXPECT_EQ(sampler.getWaveformSummary().size(), 128u);
}

TEST(BuiltinSamplerTest, NameAndPath) {
  BuiltinSampler sampler;
  EXPECT_EQ(sampler.getName(), "Sampler");
  EXPECT_EQ(sampler.getPath(), "builtin://sampler");
  EXPECT_TRUE(sampler.isInstrument());
  EXPECT_EQ(sampler.getParameterCount(), BuiltinSampler::kTotalParams);
}

}  // namespace
}  // namespace hibiki
