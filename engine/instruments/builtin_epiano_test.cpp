#include "engine/instruments/builtin_epiano.hpp"

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

TEST(BuiltinEPianoTest, SilenceWithoutNotes) {
  BuiltinEPiano epiano;
  epiano.load("", 0, 44100.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  epiano.process(nullptr, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-10f);
    EXPECT_NEAR(outR[i], 0.0f, 1e-10f);
  }
}

TEST(BuiltinEPianoTest, NoteOnProducesOutput) {
  BuiltinEPiano epiano;
  epiano.load("", 0, 44100.0);

  constexpr int N = 512;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 60;  // C4
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  ev.channel = 0;
  ev.sampleOffset = 0;
  events.push_back(ev);

  auto ctx = MakeContext();
  epiano.process(nullptr, outs, N, ctx, events);

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.01f) << "Note-on should produce audible output";
}

TEST(BuiltinEPianoTest, NoteOffSilencesAfterRelease) {
  BuiltinEPiano epiano;
  epiano.load("", 0, 44100.0);

  // Set shortest release decay
  epiano.setParameterValue(BuiltinEPiano::P_ENV_RELEASE, 0.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();

  std::vector<MidiNoteEvent> ev_on;
  MidiNoteEvent on;
  on.pitch = 60;
  on.velocity = 1.0f;
  on.isNoteOn = true;
  on.channel = 0;
  on.sampleOffset = 0;
  ev_on.push_back(on);
  epiano.process(nullptr, outs, N, ctx, ev_on);

  std::vector<MidiNoteEvent> ev_off;
  MidiNoteEvent off;
  off.pitch = 60;
  off.velocity = 0.0f;
  off.isNoteOn = false;
  off.channel = 0;
  off.sampleOffset = 0;
  ev_off.push_back(off);

  // Process a few blocks to allow release to decay
  for (int b = 0; b < 20; ++b) {
    epiano.process(nullptr, outs, N, ctx,
                   b == 0 ? ev_off : std::vector<MidiNoteEvent>{});
  }

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_LT(peak, 0.01f) << "After release, output should be near silent";
}

TEST(BuiltinEPianoTest, DisabledBypass) {
  BuiltinEPiano epiano;
  epiano.load("", 0, 44100.0);
  epiano.setParameterValue(BuiltinEPiano::P_ENABLE, 0.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 60;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  ev.channel = 0;
  ev.sampleOffset = 0;
  events.push_back(ev);

  auto ctx = MakeContext();
  epiano.process(nullptr, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-10f);
    EXPECT_NEAR(outR[i], 0.0f, 1e-10f);
  }
}

TEST(BuiltinEPianoTest, NameAndPath) {
  BuiltinEPiano epiano;
  EXPECT_EQ(epiano.getName(), "Electric Piano");
  EXPECT_EQ(epiano.getPath(), "builtin://epiano");
  EXPECT_TRUE(epiano.isInstrument());
  EXPECT_EQ(epiano.getParameterCount(), BuiltinEPiano::kTotalParams);
}

}  // namespace
}  // namespace hibiki
