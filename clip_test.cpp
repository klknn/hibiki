#include <gtest/gtest.h>
#include "clip.hpp"
#include "test_utils.hpp"

TEST(ClipTest, LoadAudioClip) {
    auto clip = hibiki::LoadClip(hibiki::find_test_file("testdata/loop140.wav"), true);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->type, hibiki::Clip::Type::AUDIO);
    EXPECT_TRUE(clip->is_loop);
    EXPECT_GT(clip->audio_data.size(), 0);
    EXPECT_GT(clip->duration_sec, 0.0);
}

TEST(ClipTest, LoadMidiClip) {
    auto clip = hibiki::LoadClip(hibiki::find_test_file("testdata/test.mid"));
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->type, hibiki::Clip::Type::MIDI);
    EXPECT_FALSE(clip->is_loop);
    EXPECT_GT(clip->midi_events.size(), 0);
    EXPECT_GT(clip->duration_sec, 0.0);
}
