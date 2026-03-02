#include <gtest/gtest.h>
#include "track.hpp"
#include "test_utils.hpp"

TEST(TrackTest, AddAndRemoveClips) {
    hibiki::Track track(0);
    auto audio_path = hibiki::find_test_file("testdata/loop140.wav");
    auto midi_path = hibiki::find_test_file("testdata/test.mid");

    EXPECT_TRUE(track.LoadClip(0, audio_path));
    EXPECT_TRUE(track.LoadClip(1, midi_path));
    
    EXPECT_EQ(track.clips.size(), 2);
    EXPECT_EQ(track.clips[0]->type, hibiki::Clip::Type::AUDIO);
    EXPECT_EQ(track.clips[1]->type, hibiki::Clip::Type::MIDI);

    track.PlayClip(0);
    EXPECT_EQ(track.playing_slot, 0);

    track.Stop();
    EXPECT_EQ(track.playing_slot, -1);

    EXPECT_TRUE(track.DeleteClip(0));
    EXPECT_EQ(track.clips.size(), 1);
}
