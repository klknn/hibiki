#include "engine/instruments/builtin_film.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
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

TEST(BuiltinFilmTest, LoadWithSyxPathProducesOutput) {
  // Simulate what BrowserPane sends: builtin://film?syx=PATH&voice=N
  // The engine should parse the syx/voice params and auto-load the DX7 voice.
  std::string syx_path = "testdata/rom1a.syx";
  std::string load_path = "builtin://film?syx=" + syx_path + "&voice=0";

  BuiltinFilm film;
  film.load(load_path, 0, 44100.0);

  // Play a note and verify output.
  constexpr int N = 1024;
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
  EXPECT_GT(peak, 0.01f)
      << "Loading via syx path should produce audible output";

  // Verify the path still starts with the expected prefix.
  EXPECT_EQ(load_path.substr(0, 14), "builtin://film");

  // After loading a DX7 voice, getParameterValue should return non-default
  // values for multiple operators (the BRASS patch has several active ops).
  // This is what the UI needs to query to update knobs.
  bool any_non_default_level = false;
  for (int op = 0; op < BuiltinFilm::kNumOps; ++op) {
    double level = film.getParameterValue(op * BuiltinFilm::kParamsPerOp +
                                          BuiltinFilm::OP_LEVEL);
    if (level > 0.01 && level < 0.99) {
      any_non_default_level = true;
    }
  }
  EXPECT_TRUE(any_non_default_level)
      << "DX7 voice should set non-trivial operator levels readable via "
         "getParameterValue";
}

TEST(BuiltinFilmTest, Dx7SysexImport) {
  // Read the DX7 ROM1A sysex testdata.
  std::ifstream file("testdata/rom1a.syx", std::ios::binary);
  ASSERT_TRUE(file.good()) << "Could not open testdata/rom1a.syx";
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  ASSERT_EQ(data.size(), 4104u);

  // Parse patch names.
  auto names = BuiltinFilm::getDx7PatchNames(data.data(), data.size());
  ASSERT_EQ(names.size(), 32u);
  // First patch in ROM1A is "BRASS   1 ".
  EXPECT_TRUE(names[0].find("BRASS") != std::string::npos)
      << "First patch name: '" << names[0] << "'";

  // Parse voices.
  BuiltinFilm::Dx7Voice voices[32];
  int count = BuiltinFilm::parseDx7Sysex(data.data(), data.size(), voices);
  ASSERT_EQ(count, 32);

  // Load voice and verify it produces audio.
  BuiltinFilm film;
  film.load("", 0, 44100.0);
  film.loadDx7Voice(voices[0]);

  constexpr int N = 1024;
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
  EXPECT_GT(peak, 0.01f) << "DX7 BRASS patch should produce audible output";
}

}  // namespace
}  // namespace hibiki
