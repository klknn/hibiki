#include "engine/instruments/builtin_3xosc.hpp"

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

TEST(Builtin3xOscTest, LeftAndRightMatchForCenteredSine) {
  Builtin3xOsc osc;
  osc.load("", 0, 44100.0);

  constexpr int N = 512;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 69;  // A4 = 440 Hz
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext(44100.0);
  osc.process(nullptr, outs, N, ctx, events);

  // Both left and right channels must match for center-panned mono oscillator.
  // Filter state corruption in left channel causes distortion and mismatch.
  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], outR[i], 1e-4f)
        << "Channel distortion mismatch at sample " << i << ": outL=" << outL[i]
        << ", outR=" << outR[i];
  }
}

TEST(Builtin3xOscTest, SineWaveFrequencyAndPurity) {
  Builtin3xOsc osc;
  double sr = 44100.0;
  osc.load("", 0, sr);

  // Set sustain to 1.0, attack to 0 to get immediate steady-state sine
  osc.setParameterValue(Builtin3xOsc::P_GAIN_A, 0.0);
  osc.setParameterValue(Builtin3xOsc::P_GAIN_D, 0.0);
  osc.setParameterValue(Builtin3xOsc::P_GAIN_S, 1.0);

  constexpr int N = 2048;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 69;  // A4 = 440 Hz
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext(sr);
  osc.process(nullptr, outs, N, ctx, events);

  // Count zero crossings (rising) to verify exact frequency
  int rising_crossings = 0;
  for (int i = 1; i < N; ++i) {
    if (outL[i - 1] <= 0.0f && outL[i] > 0.0f) {
      rising_crossings++;
    }
  }

  // Expected cycles in N samples: N * 440 / 44100 ≈ 2048 * 440 / 44100 ≈ 20.43
  // cycles
  EXPECT_GE(rising_crossings, 19);
  EXPECT_LE(rising_crossings, 21);
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
    osc.process(nullptr, outs, N, ctx,
                b == 0 ? ev_off : std::vector<MidiNoteEvent>{});
  }

  // After release, output should be near zero
  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_LT(peak, 0.01f) << "After release, output should be near silent";
}

TEST(Builtin3xOscTest, NoteOffNegativeReleasesAllVoices) {
  Builtin3xOsc osc;
  osc.load("", 0, 44100.0);
  osc.setParameterValue(Builtin3xOsc::P_GAIN_R, 0.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();

  // Note-on note 60
  std::vector<MidiNoteEvent> ev_on;
  MidiNoteEvent on;
  on.pitch = 60;
  on.velocity = 1.0f;
  on.isNoteOn = true;
  ev_on.push_back(on);
  osc.process(nullptr, outs, N, ctx, ev_on);

  // Note-off with pitch -1 (all notes off)
  std::vector<MidiNoteEvent> ev_off;
  MidiNoteEvent off;
  off.pitch = -1;
  off.velocity = 0.0f;
  off.isNoteOn = false;
  ev_off.push_back(off);

  for (int b = 0; b < 20; ++b) {
    osc.process(nullptr, outs, N, ctx,
                b == 0 ? ev_off : std::vector<MidiNoteEvent>{});
  }

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_LT(peak, 0.01f)
      << "Negative pitch note-off should release all active voices";
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
