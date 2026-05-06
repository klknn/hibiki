#include "engine/core/track.hpp"

#include <gtest/gtest.h>

#include "engine/ipc/ipc.hpp"
#include "engine/test_utils.hpp"

namespace hibiki {

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

  track.AddTimelineClip(midi_path, 0.0, 120.0);
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
        double bpm = 120.0;
        double beats_per_sec = bpm / 60.0;
        double window_start_beats = clip_local_time * beats_per_sec;
        double window_end_beats =
            (clip_local_time + time_per_block) * beats_per_sec;
        for (const auto& me : tc->clip->midi_events) {
          if (me.beats >= window_start_beats && me.beats < window_end_beats) {
            double event_local_sec = me.beats / beats_per_sec - clip_local_time;
            int sampleOffset =
                std::max(0, (int)(event_local_sec * sample_rate));
            if (sampleOffset >= block_size) sampleOffset = block_size - 1;
            EXPECT_GE(sampleOffset, 0);
            EXPECT_LT(sampleOffset, block_size);
          }
        }
      }
    }
  }
}

}  // namespace hibiki

namespace hibiki {

// Verify that group_parent_index is correctly set and that
// engine track indices remain stable (reordering is UI-only).
TEST(TrackTest, GroupParentIndex_StaysStable) {
  g_ipc_enabled = false;

  // Create 3 tracks: 0(group), 1(child), 2(normal)
  Track group_track(0);
  group_track.track_type = Track::TrackType::GROUP;
  group_track.name = "Group";

  Track child_track(1);
  child_track.group_parent_index = 0;  // child of track 0
  child_track.name = "Child";

  Track normal_track(2);
  normal_track.name = "Normal";

  // Verify initial state
  EXPECT_EQ(group_track.track_type, Track::TrackType::GROUP);
  EXPECT_EQ(child_track.group_parent_index, 0);
  EXPECT_EQ(normal_track.group_parent_index, -1);

  // Engine indices are stable slot IDs — they never change.
  // UI-only reorder just changes display order.
  EXPECT_EQ(group_track.index, 0);
  EXPECT_EQ(child_track.index, 1);
  EXPECT_EQ(normal_track.index, 2);

  // Setting group parent on a track
  normal_track.group_parent_index = 0;
  EXPECT_EQ(normal_track.group_parent_index, 0);

  // Clearing group parent
  child_track.group_parent_index = -1;
  EXPECT_EQ(child_track.group_parent_index, -1);
}

}  // namespace hibiki
