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

}  // namespace hibiki
