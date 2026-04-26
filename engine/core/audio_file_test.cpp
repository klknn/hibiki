#include "engine/core/audio_file.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "engine/test_utils.hpp"

using ::absl_testing::IsOk;
using ::testing::FloatNear;
using ::testing::Pointwise;

namespace hibiki {

TEST(AudioFileTest, LoadWav) {
  std::vector<float> data;
  int channels;
  double duration;
  EXPECT_THAT(hibiki::LoadWav(hibiki::find_test_file("testdata/loop140.wav"),
                              data, channels, duration),
              IsOk());
  EXPECT_GT(data.size(), 0);
  EXPECT_GT(channels, 0);
  EXPECT_GT(duration, 0.0);
}

TEST(AudioFileTest, SaveAndLoadWav) {
  int sample_rate = 44100;
  int channels = 2;
  int num_samples = sample_rate * 1;  // 1 second

  std::vector<float> data(num_samples * channels);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / sample_rate) *
              0.8f;  // sine wave
  }

  std::string tmp_file = "test_save.wav";
  ASSERT_THAT(hibiki::SaveWav(tmp_file, data, channels, sample_rate), IsOk());

  std::vector<float> loaded_data;
  int loaded_channels;
  double loaded_duration;
  ASSERT_THAT(
      hibiki::LoadWav(tmp_file, loaded_data, loaded_channels, loaded_duration),
      IsOk());
  EXPECT_EQ(loaded_channels, channels);
  EXPECT_EQ(loaded_data.size(), num_samples * channels)
      << "LoadWav returns interleaved samples * channels";
  EXPECT_EQ(loaded_data.size(), data.size());

  float tolerance = 1.0f / 32767.0f * 2.0f;  // 16-bit precision error.
  EXPECT_THAT(data, Pointwise(FloatNear(tolerance), loaded_data));

  std::remove(tmp_file.c_str());
}

TEST(AudioFileTest, LoadWavFileNotFound) {
  std::vector<float> data;
  int channels;
  double duration;
  EXPECT_THAT(hibiki::LoadWav("/nonexistent/path/to/file.wav", data, channels,
                              duration),
              absl_testing::StatusIs(absl::StatusCode::kNotFound,
                                     ::testing::HasSubstr("Cannot open file")));
}

TEST(AudioFileTest, LoadWavNotRiffFile) {
  // Create a temp file with garbage content
  std::string tmp = "test_not_riff.tmp";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << "NOT_RIFF_CONTENT_HERE";
  }
  std::vector<float> data;
  int channels;
  double duration;
  EXPECT_THAT(hibiki::LoadWav(tmp, data, channels, duration),
              absl_testing::StatusIs(absl::StatusCode::kInvalidArgument,
                                     ::testing::HasSubstr("Not a RIFF file")));
  std::remove(tmp.c_str());
}

TEST(AudioFileTest, SaveWavInvalidParams) {
  std::vector<float> data = {0.0f, 0.0f};
  EXPECT_THAT(
      hibiki::SaveWav("out.wav", data, /*channels=*/0, /*sample_rate=*/44100),
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument,
                             ::testing::HasSubstr("Invalid params")));
  EXPECT_THAT(
      hibiki::SaveWav("out.wav", data, /*channels=*/2, /*sample_rate=*/0),
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument,
                             ::testing::HasSubstr("Invalid params")));
}

TEST(AudioFileTest, SaveWavUnwritablePath) {
  std::vector<float> data = {0.0f, 0.0f};
  EXPECT_THAT(hibiki::SaveWav("/nonexistent/dir/output.wav", data, 1, 44100),
              absl_testing::StatusIs(absl::StatusCode::kPermissionDenied,
                                     ::testing::HasSubstr("Cannot open file")));
}

TEST(AudioFileTest, LoadAudioFileResample44100To48000) {
  // Save a 1-second mono DC signal at 44100 Hz.
  int src_rate = 44100;
  int channels = 1;
  int num_frames = src_rate;
  std::vector<float> data(num_frames, 0.6f);

  std::string tmp = "test_resample_44_48.wav";
  ASSERT_THAT(SaveWav(tmp, data, channels, src_rate), IsOk());

  // Load with target 48000 Hz — should resample.
  std::vector<float> out;
  int out_ch, out_rate;
  double out_dur;
  ASSERT_THAT(LoadAudioFile(tmp, 48000.0, out, out_ch, out_rate, out_dur),
              IsOk());
  EXPECT_EQ(out_ch, 1);
  EXPECT_EQ(out_rate, 48000);

  // Output should be ~48000 samples (1 second at 48kHz).
  EXPECT_NEAR(static_cast<int>(out.size()), 48000, 50);

  // DC value should be preserved in the middle (skip edges).
  for (size_t i = 100; i < out.size() - 100; ++i) {
    EXPECT_NEAR(out[i], 0.6f, 0.03f) << "at sample " << i;
  }
  std::remove(tmp.c_str());
}

TEST(AudioFileTest, LoadAudioFileResample48000To16000) {
  // Save a 1-second stereo DC signal at 48000 Hz.
  int src_rate = 48000;
  int channels = 2;
  int num_frames = src_rate;
  std::vector<float> data(num_frames * channels);
  for (int i = 0; i < num_frames; ++i) {
    data[2 * i] = 0.3f;      // L
    data[2 * i + 1] = 0.7f;  // R
  }

  std::string tmp = "test_resample_48_16.wav";
  ASSERT_THAT(SaveWav(tmp, data, channels, src_rate), IsOk());

  // Load with target 16000 Hz — 3:1 downsample.
  std::vector<float> out;
  int out_ch, out_rate;
  double out_dur;
  ASSERT_THAT(LoadAudioFile(tmp, 16000.0, out, out_ch, out_rate, out_dur),
              IsOk());
  EXPECT_EQ(out_ch, 2);
  EXPECT_EQ(out_rate, 16000);

  int out_frames = static_cast<int>(out.size()) / 2;
  EXPECT_NEAR(out_frames, 16000, 50);

  // DC values preserved per channel.
  for (int i = 50; i < out_frames - 50; ++i) {
    EXPECT_NEAR(out[2 * i], 0.3f, 0.03f) << "L at frame " << i;
    EXPECT_NEAR(out[2 * i + 1], 0.7f, 0.03f) << "R at frame " << i;
  }
  std::remove(tmp.c_str());
}

TEST(AudioFileTest, LoadAudioFileNoResample) {
  // Save at 44100 Hz, load with target_sample_rate <= 0 — no resampling.
  int rate = 44100;
  std::vector<float> data(rate, 0.5f);

  std::string tmp = "test_no_resample.wav";
  ASSERT_THAT(SaveWav(tmp, data, 1, rate), IsOk());

  std::vector<float> out;
  int out_ch, out_rate;
  double out_dur;
  ASSERT_THAT(LoadAudioFile(tmp, -1.0, out, out_ch, out_rate, out_dur), IsOk());
  EXPECT_EQ(out_rate, rate);
  EXPECT_EQ(out.size(), data.size());
  std::remove(tmp.c_str());
}

}  // namespace hibiki
