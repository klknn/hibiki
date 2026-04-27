#include "engine/instruments/builtin_film.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <vector>

#include "engine/test_utils.hpp"

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
  EXPECT_EQ(film.getParameterCount(), 794);
}

TEST(BuiltinFilmTest, LoadWithSyxPathProducesOutput) {
  // Simulate what BrowserPane sends: builtin://film?syx=PATH&voice=N
  // The engine should parse the syx/voice params and auto-load the DX7 voice.
  std::string syx_path = hibiki::find_test_file("testdata/rom1a.syx");
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

TEST(BuiltinFilmTest, RingModulationChangesSpectrum) {
  constexpr int N = 2048;
  auto ctx = MakeContext();

  auto renderWithMode = [&](bool rm_mode, float mod_amount) -> float {
    BuiltinFilm film;
    film.load("", 0, 44100.0);
    // Enable RM mode if requested.
    film.setParameterValue(BuiltinFilm::G_RM_MODE, rm_mode ? 1.0 : 0.0);
    // Enable op2 as modulator.
    film.setParameterValue(
        1 * BuiltinFilm::kParamsPerOp + BuiltinFilm::OP_LEVEL, 1.0);
    // Set mod matrix: op2 → op1.
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

  float fm_rms = renderWithMode(false, 0.9f);
  float rm_rms = renderWithMode(true, 0.9f);

  // FM and RM should produce audible but different output.
  EXPECT_GT(fm_rms, 0.01f);
  EXPECT_GT(rm_rms, 0.01f);
  EXPECT_NE(fm_rms, rm_rms)
      << "FM and RM modes should produce different spectral content";
}

TEST(BuiltinFilmTest, PitchEnvelopeChangesFrequency) {
  constexpr int N = 2048;
  auto ctx = MakeContext();

  auto renderWithPitchDepth = [&](float depth) -> float {
    BuiltinFilm film;
    film.load("", 0, 44100.0);
    // Set pitch envelope: fast attack, sustain at 1.0 (max pitch offset).
    film.setParameterValue(BuiltinFilm::OP_PITCH_ENV_A, 0.0);
    film.setParameterValue(BuiltinFilm::OP_PITCH_ENV_D, 0.0);
    film.setParameterValue(BuiltinFilm::OP_PITCH_ENV_S, 1.0);
    film.setParameterValue(BuiltinFilm::OP_PITCH_ENV_R, 0.0);
    film.setParameterValue(BuiltinFilm::OP_PITCH_DEPTH, depth);

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

  float no_pitch = renderWithPitchDepth(0.5f);    // neutral
  float with_pitch = renderWithPitchDepth(0.8f);  // pitch up

  EXPECT_GT(no_pitch, 0.01f);
  EXPECT_GT(with_pitch, 0.01f);
  EXPECT_NE(no_pitch, with_pitch)
      << "Pitch envelope should change the spectral content";
}

TEST(BuiltinFilmTest, MultiPointSlotOverridesADSR) {
  // When MP count > 0, the multi-point data should be used instead of ADSR.
  constexpr int N = 2048;
  auto ctx = MakeContext();

  // Render with ADSR only (count=0, default).
  BuiltinFilm adsr_film;
  adsr_film.load("", 0, 44100.0);
  float adsr_outL[N], adsr_outR[N];
  float* adsr_outs[2] = {adsr_outL, adsr_outR};
  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev{};
  ev.pitch = 60;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);
  adsr_film.process(nullptr, adsr_outs, N, ctx, events);

  // Render with a 3-point multi-point envelope (fast spike then silence).
  BuiltinFilm mp_film;
  mp_film.load("", 0, 44100.0);
  int mp_base = BuiltinFilm::kMultiPointBase;  // OP1 volume envelope (env_idx=0)
  // count = 3 (3/16 = 0.1875)
  mp_film.setParameterValue(mp_base, 3.0 / 16.0);
  // sustain index = -1 (no sustain, play through) → < 0.01
  mp_film.setParameterValue(mp_base + 1, 0.0);
  // Point 0: time=0, value=0, tension=0.5(neutral)
  mp_film.setParameterValue(mp_base + 2, 0.0);  // time
  mp_film.setParameterValue(mp_base + 3, 0.0);  // value
  mp_film.setParameterValue(mp_base + 4, 0.5);  // tension
  // Point 1: time=fast, value=1.0 (full)
  mp_film.setParameterValue(mp_base + 5, 0.0);  // fast time
  mp_film.setParameterValue(mp_base + 6, 1.0);
  mp_film.setParameterValue(mp_base + 7, 0.5);
  // Point 2: time=fast, value=0.0 (silence)
  mp_film.setParameterValue(mp_base + 8, 0.1);
  mp_film.setParameterValue(mp_base + 9, 0.0);
  mp_film.setParameterValue(mp_base + 10, 0.5);

  float mp_outL[N], mp_outR[N];
  float* mp_outs[2] = {mp_outL, mp_outR};
  mp_film.process(nullptr, mp_outs, N, ctx, events);

  // Both should produce output, but the shapes should differ.
  float adsr_rms = 0, mp_rms = 0;
  for (int i = 0; i < N; ++i) {
    adsr_rms += adsr_outL[i] * adsr_outL[i];
    mp_rms += mp_outL[i] * mp_outL[i];
  }
  adsr_rms = std::sqrt(adsr_rms / N);
  mp_rms = std::sqrt(mp_rms / N);

  EXPECT_GT(adsr_rms, 0.01f);
  EXPECT_GT(mp_rms, 0.001f);
  EXPECT_NE(adsr_rms, mp_rms)
      << "Multi-point envelope should produce different output than ADSR";
}

TEST(BuiltinFilmTest, MultiPointFallbackToADSR) {
  // With count=0, behavior should be identical to normal ADSR.
  BuiltinFilm film;
  film.load("", 0, 44100.0);
  int mp_base = BuiltinFilm::kMultiPointBase;  // OP1 (env_idx=0)
  // Ensure count is 0 (ADSR fallback).
  EXPECT_FLOAT_EQ(film.getParameterValue(mp_base), 0.0);

  constexpr int N = 512;
  auto ctx = MakeContext();
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
  EXPECT_GT(std::sqrt(rms / N), 0.01f)
      << "ADSR fallback should still produce normal output";
}

TEST(BuiltinFilmTest, Dx7SysexImport) {
  // Read the DX7 ROM1A sysex testdata.
  std::string syx_path = hibiki::find_test_file("testdata/rom1a.syx");
  std::ifstream file(syx_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Could not open " << syx_path;
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
