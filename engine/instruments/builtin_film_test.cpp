#include "engine/instruments/builtin_film.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {
namespace {

HostProcessContext MakeContext(double sr = 44100.0) {
  HostProcessContext ctx{};
  ctx.sampleRate = sr;
  return ctx;
}

TEST(BuiltinFilmTest, SilenceWithoutNotes) {
  BuiltinFilm film;
  film.load("", 0, 44100.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();
  std::vector<MidiNoteEvent> events;
  film.process(nullptr, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-10f);
    EXPECT_NEAR(outR[i], 0.0f, 1e-10f);
  }
}

TEST(BuiltinFilmTest, NoteOnProducesOutput) {
  BuiltinFilm film;
  film.load("", 0, 44100.0);

  constexpr int N = 512;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev{};
  ev.pitch = 60;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  film.process(nullptr, outs, N, ctx, events);

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.01f) << "Note-on should produce audible output";
}

TEST(BuiltinFilmTest, NoteOffSilencesAfterRelease) {
  BuiltinFilm film;
  film.load("", 0, 44100.0);
  // Short release.
  film.setParameterValue(BuiltinFilm::OP_ENV_R, 0.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();

  // Note-on.
  std::vector<MidiNoteEvent> ev_on;
  MidiNoteEvent on{};
  on.pitch = 60;
  on.velocity = 1.0f;
  on.isNoteOn = true;
  ev_on.push_back(on);
  film.process(nullptr, outs, N, ctx, ev_on);

  // Note-off.
  std::vector<MidiNoteEvent> ev_off;
  MidiNoteEvent off{};
  off.pitch = 60;
  off.velocity = 0.0f;
  off.isNoteOn = false;
  ev_off.push_back(off);

  for (int b = 0; b < 20; ++b) {
    film.process(nullptr, outs, N, ctx,
                 b == 0 ? ev_off : std::vector<MidiNoteEvent>{});
  }

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_LT(peak, 0.01f) << "After release, output should be near silent";
}

TEST(BuiltinFilmTest, DisabledBypass) {
  BuiltinFilm film;
  film.load("", 0, 44100.0);
  film.setParameterValue(BuiltinFilm::G_ENABLE, 0.0);

  constexpr int N = 256;
  float outL[N], outR[N];
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev{};
  ev.pitch = 60;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  film.process(nullptr, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-10f);
  }
}

TEST(BuiltinFilmTest, WaveformChangesTimbre) {
  constexpr int N = 1024;
  auto ctx = MakeContext();

  auto renderWithWaveform = [&](float wf_norm) -> float {
    BuiltinFilm film;
    film.load("", 0, 44100.0);
    film.setParameterValue(BuiltinFilm::OP_WAVEFORM, wf_norm);

    float outL[N], outR[N];
    float* outs[2] = {outL, outR};

    std::vector<MidiNoteEvent> events;
    MidiNoteEvent ev{};
    ev.pitch = 69;  // A4 = 440Hz
    ev.velocity = 1.0f;
    ev.isNoteOn = true;
    events.push_back(ev);

    film.process(nullptr, outs, N, ctx, events);

    // Compute RMS as a timbral fingerprint.
    float rms = 0;
    for (int i = 0; i < N; ++i) rms += outL[i] * outL[i];
    return std::sqrt(rms / N);
  };

  float sine_rms = renderWithWaveform(0.0f);    // sine
  float saw_rms = renderWithWaveform(0.3f);     // saw
  float square_rms = renderWithWaveform(0.5f);  // square

  // Different waveforms should produce different RMS levels.
  EXPECT_NE(sine_rms, saw_rms);
  EXPECT_NE(saw_rms, square_rms);
}

TEST(BuiltinFilmTest, FMModulationChangesSpectrum) {
  constexpr int N = 2048;
  auto ctx = MakeContext();

  auto renderWithMod = [&](float mod_amount) -> float {
    BuiltinFilm film;
    film.load("", 0, 44100.0);
    // Enable op2 as modulator.
    film.setParameterValue(
        1 * BuiltinFilm::kParamsPerOp + BuiltinFilm::OP_LEVEL, 1.0);
    // Set mod matrix: op2 → op1 (row 1, col 0).
    film.setParameterValue(
        BuiltinFilm::kMatrixBase + 1 * BuiltinFilm::kMatrixCols + 0,
        mod_amount);

    float outL[N], outR[N];
    float* outs[2] = {outL, outR};

    std::vector<MidiNoteEvent> events;
    MidiNoteEvent ev{};
    ev.pitch = 60;
    ev.velocity = 1.0f;
    ev.isNoteOn = true;
    events.push_back(ev);

    film.process(nullptr, outs, N, ctx, events);

    float rms = 0;
    for (int i = 0; i < N; ++i) rms += outL[i] * outL[i];
    return std::sqrt(rms / N);
  };

  float no_mod = renderWithMod(0.5f);    // neutral = no mod
  float with_mod = renderWithMod(0.9f);  // strong mod

  // FM modulation should change the spectral content (different RMS).
  EXPECT_NE(no_mod, with_mod);
}

TEST(BuiltinFilmTest, NameAndPath) {
  BuiltinFilm film;
  EXPECT_EQ(film.getName(), "FilM");
  EXPECT_EQ(film.getPath(), "builtin://film");
  EXPECT_TRUE(film.isInstrument());
  EXPECT_EQ(film.getParameterCount(), BuiltinFilm::kTotalParams);
  EXPECT_EQ(film.getParameterCount(), 253);
}

}  // namespace
}  // namespace hibiki
