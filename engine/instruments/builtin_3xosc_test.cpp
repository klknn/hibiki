#include "engine/instruments/builtin_3xosc.hpp"

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

TEST(Builtin3xOscTest, SilenceWithoutNotes) {
  Builtin3xOsc osc;
  osc.load("", 0, 44100.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  osc.process(nullptr, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-10f);
    EXPECT_NEAR(outR[i], 0.0f, 1e-10f);
  }
}

TEST(Builtin3xOscTest, NoteOnProducesOutput) {
  Builtin3xOsc osc;
  osc.load("", 0, 44100.0);

  constexpr int N = 512;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  // Send note-on as a MIDI event
  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 60;  // C4
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  osc.process(nullptr, outs, N, ctx, events);

  // Should produce non-zero output
  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.01f) << "Note-on should produce audible output";
}

TEST(Builtin3xOscTest, NoteOffSilencesAfterRelease) {
  Builtin3xOsc osc;
  osc.load("", 0, 44100.0);

  // Set very short release (norm=0 -> shortest)
  osc.setParameterValue(Builtin3xOsc::P_GAIN_R, 0.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();

  // Note-on
  std::vector<MidiNoteEvent> ev_on;
  MidiNoteEvent on;
  on.pitch = 60;
  on.velocity = 1.0f;
  on.isNoteOn = true;
  ev_on.push_back(on);
  osc.process(nullptr, outs, N, ctx, ev_on);

  // Note-off
  std::vector<MidiNoteEvent> ev_off;
  MidiNoteEvent off;
  off.pitch = 60;
  off.velocity = 0.0f;
  off.isNoteOn = false;
  ev_off.push_back(off);

  // Process many blocks to let release finish
  for (int b = 0; b < 20; ++b) {
    osc.process(nullptr, outs, N, ctx, b == 0 ? ev_off : std::vector<MidiNoteEvent>{});
  }

  // After release, output should be near zero
  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_LT(peak, 0.01f) << "After release, output should be near silent";
}

TEST(Builtin3xOscTest, DisabledBypass) {
  Builtin3xOsc osc;
  osc.load("", 0, 44100.0);
  osc.setParameterValue(Builtin3xOsc::P_ENABLE, 0.0);

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
  osc.process(nullptr, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-10f);
  }
}

TEST(Builtin3xOscTest, NameAndPath) {
  Builtin3xOsc osc;
  EXPECT_EQ(osc.getName(), "3xOsc");
  EXPECT_EQ(osc.getPath(), "builtin://3xosc");
  EXPECT_TRUE(osc.isInstrument());
  EXPECT_EQ(osc.getParameterCount(), Builtin3xOsc::kTotalParams);
}

}  // namespace
}  // namespace hibiki
