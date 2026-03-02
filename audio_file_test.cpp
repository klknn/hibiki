#include <gtest/gtest.h>
#include "audio_file.hpp"
#include "test_utils.hpp"

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
