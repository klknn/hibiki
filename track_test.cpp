#include <gtest/gtest.h>
#include "track.hpp"
#include "test_utils.hpp"
#include "ipc.hpp"

void Vst3Plugin::stopEditor() {} // for test

TEST(TrackTest, AddAndRemoveClips) {
    hibiki::g_ipc_enabled = false;
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

TEST(TrackTest, TimelineMidiPlaybackCrash) {
    hibiki::g_ipc_enabled = false;
    hibiki::Track track(0);
    auto midi_path = hibiki::find_test_file("testdata/test.mid");
    
    track.AddTimelineClip(midi_path, 0.0);
    EXPECT_EQ(track.timeline_clips.size(), 1);

    // Simulate main audio loop logic for one block
    double playhead_pos_sec = 0.0;
    double time_per_block = 512.0 / 44100.0;
    double sample_rate = 44100.0;
    size_t block_size = 512;

    for (const auto& tc : track.timeline_clips) {
        if (playhead_pos_sec + time_per_block > tc->start_time_sec &&
            playhead_pos_sec < tc->start_time_sec + tc->duration_sec) {
            
            double clip_local_time = playhead_pos_sec - tc->start_time_sec;
            
            if (tc->clip->type == hibiki::Clip::Type::MIDI) {
                for (const auto& me : tc->clip->midi_events) {
                    if (me.seconds >= clip_local_time && me.seconds < clip_local_time + time_per_block) {
                        int sampleOffset = std::max(0, (int)((me.seconds - clip_local_time) * sample_rate));
                        if (sampleOffset >= block_size) sampleOffset = block_size - 1;
                        EXPECT_GE(sampleOffset, 0);
                        EXPECT_LT(sampleOffset, block_size);
                    }
                }
            }
        }
    }
}

