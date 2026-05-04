#include "engine/core/audio_editor.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>

#include "absl/status/status_matchers.h"
#include "engine/core/audio_file.hpp"
#include "engine/test_utils.hpp"

using ::absl_testing::IsOk;

namespace hibiki {

// Helper: create a 1-second mono test WAV and return its path.
std::string createTestWav(int sample_rate, int channels, float value) {
  int num_frames = sample_rate;
  std::vector<float> data(num_frames * channels, value);
  std::string path = "test_editor.wav";
  auto s = SaveWav(path, data, channels, sample_rate);
  EXPECT_THAT(s, IsOk());
  return path;
}

TEST(AudioEditorTest, LoadAndWaveform) {
  std::string path = find_test_file("testdata/loop140.wav");
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());
  EXPECT_TRUE(editor.HasData());
  EXPECT_GT(editor.duration_sec(), 0.0);
  EXPECT_GT(editor.sample_rate(), 0);
  EXPECT_GT(editor.num_channels(), 0);

  auto waveform = editor.ComputeWaveform(256);
  EXPECT_EQ((int)waveform.size(), 256);
  // At least some peaks should be non-zero
  bool has_nonzero = false;
  for (float v : waveform) {
    if (v > 0.001f) has_nonzero = true;
  }
  EXPECT_TRUE(has_nonzero);
}

TEST(AudioEditorTest, Normalize) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  editor.Normalize();

  auto waveform = editor.ComputeWaveform(64);
  // Peak should be 1.0 after normalize
  float peak = 0;
  for (float v : waveform) peak = std::max(peak, v);
  EXPECT_NEAR(peak, 1.0f, 0.01f);

  std::remove(path.c_str());
}

TEST(AudioEditorTest, Reverse) {
  // Create a ramp signal: 0, 1, 2, ... 99
  int n = 100;
  std::vector<float> data(n);
  for (int i = 0; i < n; ++i) data[i] = (float)i / n;
  auto path = "test_reverse.wav";
  ASSERT_THAT(SaveWav(path, data, 1, 44100), IsOk());

  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 0), IsOk());
  editor.Reverse(0.0f, 1.0f);

  // After reversing, waveform should be descending
  auto wf = editor.ComputeWaveform(10);
  // First value should be higher than last
  EXPECT_GT(wf[0], wf[9]);

  std::remove(path);
}

TEST(AudioEditorTest, FadeIn) {
  auto path = createTestWav(44100, 1, 1.0f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  // Fade in the first half
  editor.FadeIn(0.0f, 0.5f);

  auto wf = editor.ComputeWaveform(4);
  // First quarter should be quieter than third quarter
  EXPECT_LT(wf[0], wf[2]);

  std::remove(path.c_str());
}

TEST(AudioEditorTest, Trim) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  double original_dur = editor.duration_sec();
  editor.Trim(0.25f, 0.75f);
  // Trimmed to half the original duration
  EXPECT_NEAR(editor.duration_sec(), original_dur / 2.0, 0.01);

  std::remove(path.c_str());
}

TEST(AudioEditorTest, ApplyGain) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  // Apply +6dB gain (roughly double)
  editor.ApplyGain(0.0f, 1.0f, 6.0f);

  auto wf = editor.ComputeWaveform(4);
  // All values should be roughly doubled
  for (float v : wf) {
    EXPECT_NEAR(v, 1.0f, 0.05f);
  }

  std::remove(path.c_str());
}

TEST(AudioEditorTest, Spectrogram) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  int w = 0, h = 0;
  auto spec = editor.ComputeSpectrogram(w, h);
  EXPECT_GT(w, 0);
  EXPECT_GT(h, 0);
  EXPECT_EQ((int)spec.size(), w * h);

  std::remove(path.c_str());
}

TEST(AudioEditorTest, SaveAndReload) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  editor.Normalize();
  std::string out_path = "test_editor_save.wav";
  ASSERT_THAT(editor.Save(out_path), IsOk());

  // Reload and verify
  AudioEditor editor2;
  ASSERT_THAT(editor2.Load(out_path, 44100.0), IsOk());
  EXPECT_NEAR(editor2.duration_sec(), editor.duration_sec(), 0.01);

  std::remove(path.c_str());
  std::remove(out_path.c_str());
}

TEST(AudioEditorTest, ConvolveWithImpulse) {
  // Create a simple source signal: impulse at sample 0
  int n = 1024;
  std::vector<float> src(n, 0.0f);
  src[0] = 1.0f;  // Impulse
  auto src_path = "test_conv_src.wav";
  ASSERT_THAT(SaveWav(src_path, src, 1, 44100), IsOk());

  // Create a simple IR: short decay
  int ir_len = 100;
  std::vector<float> ir(ir_len);
  for (int i = 0; i < ir_len; ++i) ir[i] = 1.0f / (1 + i);
  auto ir_path = "test_conv_ir.wav";
  ASSERT_THAT(SaveWav(ir_path, ir, 1, 44100), IsOk());

  AudioEditor editor;
  ASSERT_THAT(editor.Load(src_path, 0), IsOk());
  ASSERT_THAT(editor.Convolve(ir_path, 0.0f, 1.0f, true, 44100.0), IsOk());

  // After convolution of impulse with IR, output should match IR shape
  auto wf = editor.ComputeWaveform(64);
  // First point should be non-zero (the IR convolved with impulse)
  EXPECT_GT(wf[0], 0.0f);

  std::remove(src_path);
  std::remove(ir_path);
}

TEST(AudioEditorTest, BlurFlatEnvelope) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  editor.Blur(0.01f, 0);  // Flat envelope, small amount

  // Result should have same number of frames (blur doesn't extend)
  auto wf = editor.ComputeWaveform(64);
  EXPECT_EQ((int)wf.size(), 64);

  // Should have some non-zero output
  bool has_nonzero = false;
  for (float v : wf) {
    if (v > 0.001f) has_nonzero = true;
  }
  EXPECT_TRUE(has_nonzero);
  std::remove(path.c_str());
}

TEST(AudioEditorTest, BlurTriangleEnvelope) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  editor.Blur(0.05f, 1);  // Triangle envelope

  auto wf = editor.ComputeWaveform(64);
  EXPECT_EQ((int)wf.size(), 64);
  std::remove(path.c_str());
}

TEST(AudioEditorTest, BlurParabolicEnvelope) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  editor.Blur(0.05f, 2);  // Parabolic envelope

  auto wf = editor.ComputeWaveform(64);
  EXPECT_EQ((int)wf.size(), 64);
  std::remove(path.c_str());
}

TEST(AudioEditorTest, AudioDataAccessor) {
  auto path = createTestWav(44100, 1, 0.5f);
  AudioEditor editor;
  ASSERT_THAT(editor.Load(path, 44100.0), IsOk());

  const auto& data = editor.audio_data();
  EXPECT_FALSE(data.empty());
  EXPECT_EQ((int)data.size(), 44100);  // 1 second mono
  std::remove(path.c_str());
}

}  // namespace hibiki
