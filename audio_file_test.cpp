#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "audio_file.hpp"
#include "test_utils.hpp"

using ::testing::Pointwise;
using ::testing::FloatNear;

TEST(AudioFileTest, LoadWav) {
    std::vector<float> data;
    int channels;
    double duration;
    bool success = hibiki::LoadWav(hibiki::find_test_file("testdata/loop140.wav"), data, channels, duration);
    EXPECT_TRUE(success);
    EXPECT_GT(data.size(), 0);
    EXPECT_GT(channels, 0);
    EXPECT_GT(duration, 0.0);
}

TEST(AudioFileTest, SaveAndLoadWav) {
    int sample_rate = 44100;
    int channels = 2;
    int num_samples = sample_rate * 1; // 1 second
    
    std::vector<float> data(num_samples * channels);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = std::sin(2.0 * M_PI * 440.0 * i / sample_rate) * 0.8f; // sine wave
    }
    
    std::string tmp_file = "test_save.wav";
    bool save_success = hibiki::SaveWav(tmp_file, data, channels, sample_rate);
    ASSERT_TRUE(save_success);
    
    std::vector<float> loaded_data;
    int loaded_channels;
    double loaded_duration;
    bool load_success = hibiki::LoadWav(tmp_file, loaded_data, loaded_channels, loaded_duration);
    
    ASSERT_TRUE(load_success);
    EXPECT_EQ(loaded_channels, channels);
    EXPECT_EQ(loaded_data.size(), num_samples * channels)
        << "LoadWav returns interleaved samples * channels";
    EXPECT_EQ(loaded_data.size(), data.size());
    
    float tolerance = 1.0f / 32767.0f * 2.0f; // 16-bit precision error.
    EXPECT_THAT(data, Pointwise(FloatNear(tolerance), loaded_data));
    
    std::remove(tmp_file.c_str());
}
